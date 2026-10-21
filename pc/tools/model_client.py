#!/usr/bin/env python3
"""The bake-off and pipeline tools' model client: one chat call to an
OpenAI-compatible endpoint, OpenRouter (the default) or a local server
(vLLM, llama.cpp; a loopback host).

    --base-url URL        default https://openrouter.ai/api/v1
    --api-key-env NAME    the environment variable holding the key (never
                          printed or logged); default OPENROUTER_API_KEY for a
                          remote endpoint, none for a local one (no
                          Authorization header is sent then)
    --thinking on|off|default   on/off: `chat_template_kwargs.enable_thinking`
                          (local: Qwen-style templates) or OpenRouter's
                          `reasoning.enabled` (remote); default sends nothing
    --temperature T       default 0

A remote call's messages go through pc/tools/prompt_privacy.py and are
refused if anything local is left; a local call's are sanitised the same
way but never refused (nothing leaves the machine). A local call's
max_tokens is clamped to what the server's context leaves after the prompt
(its /v1/models max_model_len and its /tokenize count, or an estimate).
Every call returns its text and a record: model, endpoint kind, tokens,
seconds, cost (0 for a local server), the sampling settings.
"""
import dataclasses
import json
import os
import re
import time
import urllib.error
import urllib.parse
import urllib.request

import prompt_privacy

DEFAULT_BASE_URL = "https://openrouter.ai/api/v1"
DEFAULT_KEY_ENV = "OPENROUTER_API_KEY"
LOCAL_HOSTS = {"127.0.0.1", "localhost", "::1"}


class ModelError(Exception):
    """A call failed (HTTP error, server down, no choices): stop, do not retry the step."""


class ContextError(ModelError):
    """The conversation does not fit the server's context."""


@dataclasses.dataclass
class Endpoint:
    base_url: str = DEFAULT_BASE_URL
    api_key_env: str | None = None  # None: the default for the endpoint's kind
    thinking: str = "default"
    temperature: float = 0.0
    title: str = "rood-boi port pipeline"
    timeout: int | None = None
    local: bool | None = None  # None: from the host
    _context: int | None = dataclasses.field(default=None, repr=False)

    def __post_init__(self):
        self.base_url = self.base_url.rstrip("/")
        if self.local is None:
            host = urllib.parse.urlparse(self.base_url).hostname or ""
            self.local = host in LOCAL_HOSTS or host.endswith(".localhost")
        if self.api_key_env is None:
            self.api_key_env = "" if self.local else DEFAULT_KEY_ENV
        if self.thinking not in ("on", "off", "default"):
            raise ValueError(f"thinking: {self.thinking!r} (on, off or default)")
        if self.timeout is None:
            self.timeout = 1800 if self.local else 600

    @property
    def kind(self):
        return "local" if self.local else "remote"

    def is_default(self):
        return (self.base_url == DEFAULT_BASE_URL and self.thinking == "default" and self.temperature == 0
                and self.api_key_env == DEFAULT_KEY_ENV)

    def key_fields(self):
        """What, besides the model and the messages, makes a saved reply this endpoint's (empty for the default)."""
        if self.is_default():
            return {}
        return {"endpoint": self.kind, "thinking": self.thinking, "temperature": self.temperature}

    def missing_key(self):
        """The environment variable the endpoint needs and lacks, or ''."""
        return self.api_key_env if self.api_key_env and not os.environ.get(self.api_key_env) else ""

    def _headers(self):
        headers = {"Content-Type": "application/json"}
        if self.api_key_env:
            key = os.environ.get(self.api_key_env)
            if not key:
                raise ModelError(f"{self.api_key_env} is not set")
            headers["Authorization"] = "Bearer " + key
        if not self.local:
            headers["X-Title"] = self.title
        return headers

    def _get(self, url, body=None):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(url, data=data, headers=self._headers())
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.loads(response.read())

    def context(self, model):
        """The server's max_model_len for `model` (local only; None when it does not say)."""
        if self._context is None:
            try:
                data = self._get(self.base_url + "/models")
                lens = [m.get("max_model_len") for m in data.get("data", []) if m.get("id") == model]
                self._context = int(lens[0]) if lens and lens[0] else 0
            except (urllib.error.URLError, OSError, ValueError, json.JSONDecodeError):
                self._context = 0
        return self._context or None

    def prompt_tokens(self, model, messages):
        """vLLM's /tokenize count of the conversation (chat template applied), else an estimate."""
        root = re.sub(r"/v1$", "", self.base_url)
        try:
            data = self._get(root + "/tokenize", {"model": model, "messages": messages, "add_generation_prompt": True})
            if isinstance(data.get("count"), int):
                return data["count"]
        except (urllib.error.URLError, OSError, ValueError, json.JSONDecodeError):
            pass
        return sum(len(m["content"]) for m in messages) // 3 + 16 * len(messages)


def strip_thinking(text):
    """The answer without a reasoning block a template left in the content."""
    text = re.sub(r"<think>.*?</think>\s*", "", text, flags=re.S)
    if "</think>" in text:  # the template opened it in the prompt
        text = text.rsplit("</think>", 1)[1].lstrip()
    return text


def call(messages, model, max_tokens, endpoint=None):
    """One chat completion: dict(text, finish_reason, model, endpoint, tokens, cost, seconds, ...)."""
    endpoint = endpoint or Endpoint()
    if endpoint.local:
        messages = [dict(m, content=prompt_privacy.sanitise(m["content"])) for m in messages]
    else:
        # Every message goes through the one sanitiser, and is refused if anything local is left.
        messages = [dict(m, content=prompt_privacy.check(prompt_privacy.sanitise(m["content"]))) for m in messages]
    body = {"model": model, "messages": messages, "temperature": endpoint.temperature, "max_tokens": max_tokens}
    if endpoint.local:
        context = endpoint.context(model)
        if context:
            used = endpoint.prompt_tokens(model, messages)
            room = context - used - 64
            if room < 1024:
                raise ContextError(f"the conversation ({used} tokens) leaves {room} of the server's {context}")
            body["max_tokens"] = min(max_tokens, room)
        if endpoint.thinking != "default":
            body["chat_template_kwargs"] = {"enable_thinking": endpoint.thinking == "on"}
    else:
        body["usage"] = {"include": True}
        if endpoint.thinking != "default":
            body["reasoning"] = {"enabled": endpoint.thinking == "on"}
    request = urllib.request.Request(endpoint.base_url + "/chat/completions", data=json.dumps(body).encode(),
                                     headers=endpoint._headers())
    start = time.monotonic()
    data = None
    for attempt in range(3):
        try:
            with urllib.request.urlopen(request, timeout=endpoint.timeout) as response:
                data = json.loads(response.read())
            break
        except urllib.error.HTTPError as error:
            detail = error.read().decode(errors="replace")[:500]
            if error.code in (429, 500, 502, 503) and attempt < 2:
                time.sleep(10 * (attempt + 1))
                continue
            if error.code == 400 and re.search(r"context|max_model_len|too long", detail, re.I):
                raise ContextError(f"HTTP 400: {detail}")
            raise ModelError(f"{endpoint.kind} endpoint: HTTP {error.code}: {detail}")
        except (urllib.error.URLError, OSError) as error:
            reason = getattr(error, "reason", error)
            if endpoint.local or attempt == 2:
                raise ModelError(f"{endpoint.kind} endpoint unreachable: {reason}")
            time.sleep(10 * (attempt + 1))
    if not data or "choices" not in data:
        raise ModelError(f"{endpoint.kind} endpoint: no choices: {json.dumps(data)[:500]}")
    usage = data.get("usage") or {}
    message = data["choices"][0]["message"]
    reasoning = message.get("reasoning_content") or message.get("reasoning") or ""
    return {
        "text": strip_thinking(message.get("content") or ""),
        "finish_reason": data["choices"][0].get("finish_reason"),
        "model": data.get("model") or model,
        "endpoint": endpoint.kind,
        "thinking": endpoint.thinking,
        "temperature": endpoint.temperature,
        "max_tokens": body["max_tokens"],
        "prompt_tokens": usage.get("prompt_tokens"),
        "completion_tokens": usage.get("completion_tokens"),
        "reasoning_tokens": (usage.get("completion_tokens_details") or {}).get("reasoning_tokens")
        or (len(reasoning) // 4 if reasoning else None),
        "cost": 0.0 if endpoint.local else (usage.get("cost") or 0.0),
        "seconds": round(time.monotonic() - start, 1),
    }


def add_arguments(parser):
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL,
                        help="an OpenAI-compatible endpoint (default OpenRouter; a loopback host is local: no key, no cost)")
    parser.add_argument("--api-key-env", default=None,
                        help="the variable holding the key (default OPENROUTER_API_KEY remote, none local; '' for none)")
    parser.add_argument("--thinking", choices=("on", "off", "default"), default="default",
                        help="ask for reasoning on or off (local: chat_template_kwargs.enable_thinking)")
    parser.add_argument("--temperature", type=float, default=0.0)


def from_args(args, title="rood-boi port pipeline"):
    return Endpoint(base_url=args.base_url, api_key_env=args.api_key_env, thinking=args.thinking,
                    temperature=args.temperature, title=title)
