# Vendored third-party code

Everything here is a single header, copied verbatim from upstream at a pinned revision. The project never downloads
anything at configure time, so it builds offline and behind restrictive proxies.

| Library | Version / revision | SHA-256 of the header | License |
|---|---|---|---|
| [doctest](https://github.com/doctest/doctest) `doctest/doctest.h` | tag `v2.4.11` (`ae7a135`) | `44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb` | MIT |
| [miniaudio](https://github.com/mackron/miniaudio) `miniaudio.h` | tag `0.11.25` (`9634bed`) | `ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89` | Public domain or MIT-0 |
| [dr_wav](https://github.com/mackron/dr_libs) `dr_wav.h` | commit `dfe8377` (reports v0.14.6) | `03e70c1a2d9787cd7ed3e966c075bea7bac6373f759db9cd7ca9ccdfc4ec4493` | Public domain or MIT-0 |

All of these are compatible with the project's GPL-3.0 license.

To update one, fetch the new revision from the raw URL of its upstream tag or commit, replace the file, and update the
table above with the new revision and hash (`sha256sum <file>`).

The Qt toolkit (LGPL-3.0) and PipeWire (MIT) are *not* vendored. They are used as system libraries and linked
dynamically.
