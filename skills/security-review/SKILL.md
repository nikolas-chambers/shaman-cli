---
name: security-review
description: Audit code or a change for exploitable vulnerabilities - injection, authz, secrets, crypto, SSRF, deserialization, supply chain - and propose concrete fixes. Use for security reviews and before shipping sensitive features.
---
# Security review

Think like an attacker: where does untrusted input enter, and what can it reach?

## Map the surface
- Entry points: HTTP handlers, CLI args, file uploads, message queues, webhooks, environment, config files, deserialised data.
- Trust boundaries: who is the caller, how are they authenticated, what are they allowed to do.
- Sensitive sinks: database queries, shell commands, file paths, templates/HTML, redirects, outbound HTTP, crypto, logs.

## Check
- **Injection**: parameterised queries only; no string-built SQL, shell or LDAP; argv arrays instead of `sh -c`; path joins normalised and confined to a base dir; output encoding for HTML/JS contexts.
- **AuthN/AuthZ**: every new route checks identity *and* ownership (IDOR); admin paths are not guarded only by the UI; tokens validated (signature, expiry, audience).
- **Secrets**: none in code, tests, logs, error messages or client bundles; loaded from env/secret store; rotated if exposed.
- **Crypto**: vetted libraries; no custom crypto; CSPRNG for tokens; modern algorithms (no MD5/SHA1 for passwords, use argon2/bcrypt/scrypt); constant-time comparison for secrets.
- **SSRF / outbound**: user-supplied URLs are allow-listed; internal ranges blocked; redirects re-checked.
- **Deserialisation & parsing**: no unsafe deserialisers on untrusted data; size limits; XML external entities disabled.
- **Web**: CSRF protection on state-changing requests, strict CORS, secure cookies (HttpOnly, Secure, SameSite), security headers.
- **Dependencies**: known-vulnerable versions, typosquats, install scripts, pinned versions and lockfiles.
- **Resource abuse**: rate limits, pagination caps, timeouts, upload size limits.

## Report
Per finding: severity (critical/high/medium/low), `path:line`, the exploit in one or two concrete steps, impact, and the fix. No theoretical issues without a plausible path.
