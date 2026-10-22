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
    --reasoning-budget N  cap the hidden reasoning at N tokens (OpenRouter's
                          `reasoning.max_tokens`: Anthropic's thinking budget,
                          at least 1024; remote only); default: the model's own.
                          Claude Haiku 5.5 (adaptive thinking) ignores it:
                          BAKEOFF.md, "Batches and rounds"
    --reasoning-effort E  minimal|low|medium|high: OpenRouter's
                          `reasoning.effort` (Haiku 5.5 honours it: fewer
                          reasoning tokens); remote only

A remote call's messages go through pc/tools/prompt_privacy.py and are
refused if anything local is left; a local call's are sanitised the same
way but never refused (nothing leaves the machine). A local call's
max_tokens is clamped to what the server's context leaves after the prompt
(its /v1/models max_model_len and its /tokenize count, or an estimate).
Every call returns its text and a record: model, endpoint kind, tokens,
seconds, cost (0 for a local server), the sampling settings.

Batches (OpenRouter's asynchronous Batch API, half the price; port_pipeline's
--mode rounds): `request_body` is what `call` would send, `submit_batch`
posts many at once (POST /batches, one model, `custom_id` per request) and
`get_batch` polls one (GET /batches/{id}: validating, in_progress,
finalizing, then completed/failed/expired/cancelled; `results` only once
completed); `parse_reply` turns a result's body into the same record as a
call's (its cost from the batch price list when the body has none).
"""
import dataclasses
import http.client
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
    reasoning_budget: int | None = None  # OpenRouter's reasoning.max_tokens (remote only)
    reasoning_effort: str | None = None  # OpenRouter's reasoning.effort (remote only)
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
        if self.reasoning_budget is not None and self.reasoning_budget < 1024:
            raise ValueError(f"reasoning budget {self.reasoning_budget}: at least 1024 (Anthropic's minimum)")
        if self.reasoning_effort not in (None, "minimal", "low", "medium", "high"):
            raise ValueError(f"reasoning effort {self.reasoning_effort!r} (minimal, low, medium or high)")

    @property
    def kind(self):
        return "local" if self.local else "remote"

    def is_default(self):
        return (self.base_url == DEFAULT_BASE_URL and self.thinking == "default" and self.temperature == 0
                and self.api_key_env == DEFAULT_KEY_ENV and self.reasoning_budget is None
                and self.reasoning_effort is None)

    def key_fields(self):
        """What, besides the model and the messages, makes a saved reply this endpoint's (empty for the default)."""
        if self.is_default():
            return {}
        out = {"endpoint": self.kind, "thinking": self.thinking, "temperature": self.temperature}
        if self.reasoning_budget is not None:
            out["reasoning_budget"] = self.reasoning_budget
        if self.reasoning_effort is not None:
            out["reasoning_effort"] = self.reasoning_effort
        return out

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

    def _get(self, url, body=None, method=None, timeout=30):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(url, data=data, headers=self._headers(), method=method)
        with urllib.request.urlopen(request, timeout=timeout) as response:
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


def request_body(messages, model, max_tokens, endpoint=None):
    """The chat-completions body `call` sends (messages sanitised; a remote
    one refused if anything local is left)."""
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
        if endpoint.thinking == "off":
            body["reasoning"] = {"enabled": False}
        elif endpoint.reasoning_budget is not None:
            # Anthropic: the thinking budget; max_tokens must stay above it.
            body["reasoning"] = {"max_tokens": endpoint.reasoning_budget}
            body["max_tokens"] = max(body["max_tokens"], endpoint.reasoning_budget + 1024)
        elif endpoint.reasoning_effort is not None:
            body["reasoning"] = {"effort": endpoint.reasoning_effort}
        elif endpoint.thinking == "on":
            body["reasoning"] = {"enabled": True}
    return body


def call(messages, model, max_tokens, endpoint=None):
    """One chat completion: dict(text, finish_reason, model, endpoint, tokens, cost, seconds, ...)."""
    endpoint = endpoint or Endpoint()
    body = request_body(messages, model, max_tokens, endpoint)
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
        except (urllib.error.URLError, OSError, http.client.HTTPException) as error:
            # (a connection cut mid-reply is an http.client.IncompleteRead: asked again too)
            reason = getattr(error, "reason", error)
            if endpoint.local or attempt == 2:
                raise ModelError(f"{endpoint.kind} endpoint unreachable: {reason}")
            time.sleep(10 * (attempt + 1))
    return parse_reply(data, model, body, endpoint, round(time.monotonic() - start, 1))


def parse_reply(data, model, body, endpoint, seconds, prices=None):
    """A chat-completions response as a call record. `prices` ((prompt, completion)
    US$ per token): the cost when the response carries none (a batch result)."""
    if not data or "choices" not in data or not data["choices"]:
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
        "reasoning_tokens": reported if (reported := (usage.get("completion_tokens_details") or {})
                                         .get("reasoning_tokens")) is not None
        else (len(reasoning) // 4 if reasoning else None),
        "cost": 0.0 if endpoint.local else (usage.get("cost") or (
            (usage.get("prompt_tokens") or 0) * prices[0] + (usage.get("completion_tokens") or 0) * prices[1]
            if prices else 0.0)),
        "seconds": seconds,
        **({"reasoning_budget": endpoint.reasoning_budget} if endpoint.reasoning_budget is not None else {}),
        **({"reasoning_effort": endpoint.reasoning_effort} if endpoint.reasoning_effort is not None else {}),
    }


# ---------------------------------------------------------------- batches
BATCH_FINAL = ("completed", "failed", "expired", "cancelled")


def submit_batch(requests, model, endpoint=None, chat_endpoint="/v1/chat/completions"):
    """POST /batches: requests = [(custom_id, body)], one model; the batch object."""
    endpoint = endpoint or Endpoint()
    # The metadata goes before `requests` (OpenRouter streams the body).
    payload = {"endpoint": chat_endpoint, "model": model, "completion_window": "24h",
               "requests": [{"custom_id": cid, "body": {k: v for k, v in body.items() if k != "model"}}
                            for cid, body in requests]}
    try:
        return endpoint._get(endpoint.base_url + "/batches", payload, method="POST", timeout=300)
    except urllib.error.HTTPError as error:
        raise ModelError(f"batch submit: HTTP {error.code}: {error.read().decode(errors='replace')[:500]}") from None
    except (urllib.error.URLError, OSError) as error:
        raise ModelError(f"batch submit: unreachable: {getattr(error, 'reason', error)}") from None


def get_batch(batch_id, endpoint=None):
    """GET /batches/{id}: the batch object (`results` once `completed`)."""
    endpoint = endpoint or Endpoint()
    try:
        return endpoint._get(endpoint.base_url + "/batches/" + urllib.parse.quote(batch_id), timeout=120)
    except urllib.error.HTTPError as error:
        raise ModelError(f"batch {batch_id}: HTTP {error.code}: {error.read().decode(errors='replace')[:300]}") from None
    except (urllib.error.URLError, OSError) as error:
        raise ModelError(f"batch {batch_id}: unreachable: {getattr(error, 'reason', error)}") from None


def batch_prices(model, endpoint=None):
    """(prompt, completion) US$ per token of `model`'s batch variant (OpenRouter's
    /models/<model>:batch/endpoints), or None."""
    endpoint = endpoint or Endpoint()
    try:
        data = endpoint._get(f"{endpoint.base_url}/models/{model}:batch/endpoints")
        p = data["data"]["endpoints"][0]["pricing"]
        return float(p["prompt"]), float(p["completion"])
    except (urllib.error.URLError, OSError, ValueError, KeyError, IndexError, TypeError, json.JSONDecodeError):
        return None


def add_arguments(parser):
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL,
                        help="an OpenAI-compatible endpoint (default OpenRouter; a loopback host is local: no key, no cost)")
    parser.add_argument("--api-key-env", default=None,
                        help="the variable holding the key (default OPENROUTER_API_KEY remote, none local; '' for none)")
    parser.add_argument("--thinking", choices=("on", "off", "default"), default="default",
                        help="ask for reasoning on or off (local: chat_template_kwargs.enable_thinking)")
    parser.add_argument("--temperature", type=float, default=0.0)
    parser.add_argument("--reasoning-budget", type=int, default=None, metavar="N",
                        help="cap hidden reasoning at N tokens (OpenRouter reasoning.max_tokens; >= 1024; "
                             "Haiku 5.5 ignores it)")
    parser.add_argument("--reasoning-effort", choices=("minimal", "low", "medium", "high"), default=None,
                        help="OpenRouter reasoning.effort (Haiku 5.5: fewer reasoning tokens)")


def from_args(args, title="rood-boi port pipeline"):
    return Endpoint(base_url=args.base_url, api_key_env=args.api_key_env, thinking=args.thinking,
                    temperature=args.temperature, title=title,
                    reasoning_budget=getattr(args, "reasoning_budget", None),
                    reasoning_effort=getattr(args, "reasoning_effort", None))
