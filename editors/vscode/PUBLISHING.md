# Publishing the Dux Language Extension

This document covers how to package and publish the extension to both the
**Visual Studio Marketplace** (used by VS Code) and **Open VSX** (used by
Cursor and VSCodium).

---

## Prerequisites

```bash
npm install -g @vscode/vsce ovsx
```

Or install them as local dev dependencies (already listed in `package.json`):

```bash
npm install
npx vsce --version
npx ovsx --version
```

---

## 1 — Create a Publisher Account (VS Marketplace)

1. Go to [marketplace.visualstudio.com/manage](https://marketplace.visualstudio.com/manage).
2. Sign in with a Microsoft account.
3. Click **Create publisher**.
4. Set the publisher ID to `dux-lang` (must match the `"publisher"` field in `package.json`).
5. Fill in the display name and save.

### Generate a Personal Access Token (PAT)

1. Visit [dev.azure.com](https://dev.azure.com) → your organisation → **User settings** → **Personal Access Tokens**.
2. Click **New Token**.
3. Set **Scopes** → **Marketplace** → check **Manage**.
4. Copy the token — you will not see it again.

---

## 2 — Log In with vsce

```bash
vsce login dux-lang
# Paste your PAT when prompted
```

Your credentials are stored in the system keychain and reused for future
`vsce publish` calls.

---

## 3 — Package the Extension (`.vsix`)

```bash
# From the editors/vscode directory:
vsce package

# Or via the npm script defined in package.json:
npm run package
```

This produces a file like `dux-lang-0.1.3.vsix` in the current directory.

### Manual install from `.vsix`

```bash
# VS Code
code --install-extension dux-lang-0.1.3.vsix

# Cursor
cursor --install-extension dux-lang-0.1.3.vsix
```

---

## 4 — Publish to the Visual Studio Marketplace

```bash
vsce publish

# Or via npm script:
npm run publish
```

To publish a specific version bump at the same time:

```bash
vsce publish patch   # 0.1.3 -> 0.1.4
vsce publish minor   # 0.1.3 -> 0.2.0
vsce publish major   # 0.1.3 -> 1.0.0
```

To publish a pre-built `.vsix` without repackaging:

```bash
vsce publish --packagePath dux-lang-0.1.3.vsix
```

---

## 5 — Publish to Open VSX (Cursor / VSCodium)

[Open VSX](https://open-vsx.org) is the registry used by Cursor and VSCodium.
Publishing here makes the extension discoverable directly from their Extensions
panels.

### Create an Open VSX account

1. Go to [open-vsx.org](https://open-vsx.org) and sign in with GitHub.
2. Navigate to your user settings → **Access Tokens** → **Generate New Token**.
3. Copy the token.

### Log In

```bash
ovsx login dux-lang --pat <YOUR_OPEN_VSX_TOKEN>
```

Or set the environment variable to avoid the interactive prompt:

```bash
export OVSX_PAT=<YOUR_OPEN_VSX_TOKEN>
```

### Publish

```bash
ovsx publish dux-lang-0.1.3.vsix --pat <YOUR_OPEN_VSX_TOKEN>

# Or via npm script (uses OVSX_PAT env var):
npm run publish:ovsx
```

The `npm run publish:ovsx` script calls `ovsx publish` which expects the
`.vsix` to already exist (run `npm run package` first) and the `OVSX_PAT`
environment variable to be set.

---

## 6 — Recommended Release Workflow

```bash
# 1. Bump the version in package.json and commit/tag
git add package.json
git commit -m "chore: release v0.1.4"
git tag v0.1.4

# 2. Package
npm run package          # produces dux-lang-0.1.4.vsix

# 3. Publish to VS Marketplace
npm run publish          # or: vsce publish --packagePath dux-lang-0.1.4.vsix

# 4. Publish to Open VSX
export OVSX_PAT=<token>
ovsx publish dux-lang-0.1.4.vsix

# 5. Push the tag
git push origin v0.1.4
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `Missing publisher name` | Ensure `"publisher": "dux-lang"` is in `package.json` |
| `Publisher 'dux-lang' not found` | Create the publisher at marketplace.visualstudio.com |
| `401 Unauthorized` | Re-run `vsce login dux-lang` with a valid PAT |
| `icon.png not found` | Add an `icon.png` (128×128 px) or remove the `"icon"` field from `package.json` |
| `OVSX 401` | Run `ovsx login dux-lang --pat <token>` or set `OVSX_PAT` |
| Cursor can't find the extension | Publish to Open VSX; Cursor uses OpenVSX, not the VS Marketplace |

---

## Useful Links

- [VS Code Publishing docs](https://code.visualstudio.com/api/working-with-extensions/publishing-extension)
- [vsce CLI reference](https://github.com/microsoft/vscode-vsce)
- [Open VSX registry](https://open-vsx.org)
- [ovsx CLI reference](https://github.com/eclipse/openvsx/wiki/Publishing-Extensions)
