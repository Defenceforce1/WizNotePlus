# WizNotePlus AI development

Updated: 2026-11-03

Dedicated branch: `feature/ai-development`.
Base: reviewed build397 source at `8493c8d449b8038a79b7e6a346512b024e62643b`.

AI development is managed separately from stable maintenance. Initialization only; no AI feature or service has been selected or implemented.

## Decisions pending

- First user-facing feature and its input/output scope.
- Local models or remote provider, credentials and cost boundaries.
- Entry point, cancellation, errors, result display and explicit write-back.
- Explicit consent for sending note content; encrypted-note handling and retention.
- Synthetic acceptance tests and user experience checks.

## Constraints

Preserve note content, formulas, references, PDF modes, password handling and save reliability. AI must be optional and avoid unnecessary idle requests or resource use. Do not access production notes or credentials, invoke paid services, upload notes, install or replace the production application without explicit authorization. Keep local diagnostic data and packages out of this public repository.

## Progress

2026-11-03: dedicated local checkout and chat initialized. Remote branch established from the reviewed release source, with this sanitized development outline. Local snapshot history is separate; do not force-push it over the reviewed remote history. No automatic ongoing synchronization is implied.
