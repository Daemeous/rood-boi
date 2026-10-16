# Texture format helpers

- `xxhash.h`: xxHash 0.8.3, https://github.com/Cyan4973/xxHash/tree/v0.8.3, BSD 2-Clause. Header includes its license. Used for DuckStation XXH3 matching; not used for Beetle CRC32.
- `bcdec.h`: https://github.com/iOrange/bcdec/commit/80859ed3b7afb1c527a2a99d70c61457bea72d0c, MIT option (license in header). Used for DDS block decoding, including BC7.

Vendored at fixed revisions; builds do not download these libraries. No texture pack is included.
