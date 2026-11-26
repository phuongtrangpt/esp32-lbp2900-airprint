# One-time web access

The existing Cloudflare Pages project `vietphuong-esp` serves `/lay-ma/` and a D1-backed `/api/access/` API. Each code lasts five minutes and is redeemed once using a transactional D1 batch. Session tokens live only in tab memory (server upper bound 24h); reload requires another code. Firmware binaries and device OTA endpoints stay unchanged and public, so this is a website entry flow, not firmware DRM. No Facebook-origin restriction.

The `key-access.yml` workflow builds only the installer, preserving all nine published firmware profiles byte-for-byte. On `web/access-codes` it deploys a preview; on `main` it deploys the existing production Pages project. It needs the existing GitHub Cloudflare account/token secrets, with Account / Cloudflare Pages / Edit plus Account / D1 / Edit. No secret is included in the browser or Git. The database is created idempotently by name; expiry cleanup happens on issuance/redemption. Codes and tokens are stored as SHA-256 hashes.

When ready, attach `key.vietphuong.io.vn` as another custom domain on the same Pages project, then add its Cloudflare DNS record. Its root redirects to `/lay-ma/`. Until then, share `https://esp.vietphuong.io.vn/lay-ma/`. Current code-page links (Facebook support and installer) can be extended in `installer/public/lay-ma/index.html`; do not add placeholder Shopee links.

To revoke all outstanding codes, execute `DELETE FROM access_codes;` in the database console. Existing unlocked tabs remain usable. To revoke sessions too, clear `access_sessions`. No client-side admin endpoint is exposed.
