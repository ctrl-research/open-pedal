# Contributing

## Getting Started

1. Fork the repository and clone your fork
2. Install the pinned tools from `.tool-versions`: `mise install` (or `asdf install`)
3. Build and test:
   ```sh
   cmake --preset default
   cmake --build --preset default
   ctest --preset default
   ```
4. Create a branch: `git checkout -b feat/your-feature-name`
5. Make your changes, keeping the build warning-free and adding tests
6. Commit using [conventional commits](#commit-style)
7. Push and open a Pull Request

### Contributing a pedal

Example pedals live in `pedals/examples/` and are bundled into the plugin. Validate yours with
`open-pedal-render --check path/to/pedal.json` and make sure `ctest` still passes: the
`ExamplePedalTests` suite runs every bundled pedal through silence, a sine, and every knob extreme.

### Contributing a DSP block

See "Adding a block" in `AGENTS.md`. Every block parameter needs a one-line `doc`; the block
reference in `docs/PEDAL_FORMAT.md` is generated from it.

## Branch Naming

Branches should follow: `feat|bug|hotfix|release|chore/brief-3-5-word-description`

Examples:
- `feat/add-user-auth`
- `bug/fix-login-crash`
- `hotfix/security-patch`
- `chore/update-dependencies`

## Commit Style

Use [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <description>

[optional body]

[optional footer]
```

Types: `feat`, `fix`, `chore`, `docs`, `test`, `refactor`, `perf`, `ci`

Examples:
- `feat(auth): add OAuth2 login support`
- `fix(api): correct response status code for 404`
- `chore(deps): bump JUCE to 9.0.3`

## Versioning

Project artifacts (releases, tags, packages, container images) follow [Semantic Versioning](https://semver.org/):

- Format is bare `X.Y.Z` — **no prefix** (`1.4.2`, not `v1.4.2`)
- **MAJOR** — breaking changes
- **MINOR** — backwards-compatible features
- **PATCH** — backwards-compatible fixes

## Pull Requests

- Label the PR `major`, `minor`, or `patch` to control the version bump when it merges. Unlabeled
  PRs bump the patch version. Every merge to `main` publishes a release automatically.
- Fill out the PR template completely
- Link any related issues
- Ensure CI passes before requesting review
- Maintainers will review within 48 hours

## Code Standards

- Write clear, commented code
- Add tests for new functionality
- Update documentation for user-facing changes
