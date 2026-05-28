# Dux Lang — Claude Code Instructions

## Git Conventions

### Branch naming
Always use descriptive branch names following conventional commits style:

```
<type>/<short-description>
```

Examples:
- `feat/editor-support`
- `fix/runtime-path-resolution`
- `chore/gitignore-claude`
- `docs/readme-why-section`
- `refactor/stdlib-option-result`

Never use generic names like `editors-integration`, `my-branch`, or system-generated names.

### Commits
- Author: `Matheus Santos <vorj.dux@gmail.com>`
- No references to Claude, Anthropic, or AI tools in commit messages, code comments, PR titles/bodies, or any pushed artifacts.
- Never include Claude session links (claude.ai/code/session_...) in PR descriptions, commit messages, or any pushed content.
- Never use em dashes (--) in commit messages, comments, docs, or any text. Use a plain hyphen (-) or reword instead.