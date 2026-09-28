---
name: terraform
description: Write, refactor and review Terraform/OpenTofu infrastructure code - modules, state, plans, imports, drift - safely. Use for .tf files, infrastructure changes and IaC reviews.
---
# Terraform / OpenTofu

## Rules
- Never apply without reading the plan. Run `terraform plan -out=tfplan`, review every `destroy`/`replace`, then `apply tfplan`.
- Remote state with locking (S3 + DynamoDB, GCS, Terraform Cloud); never commit `.tfstate` or `.terraform/`.
- Pin provider and module versions (`required_providers` with `~>` constraints) and commit `.terraform.lock.hcl`.
- No secrets in `.tf` files or variables defaults; use a secret manager or `sensitive = true` inputs from CI.

## Structure
- Small, composable modules with clear inputs/outputs; environments (dev/prod) as separate root modules or workspaces, not copy-paste.
- `variables.tf` with types, descriptions and validation; `outputs.tf` for what other stacks need.
- `for_each` over maps (stable keys) instead of `count` for collections that change.
- Tag/label every resource (owner, env, cost centre) via `default_tags` where the provider supports it.

## Refactoring without destroying
Use `moved {}` blocks (or `terraform state mv`) when renaming resources or moving them into modules; `import {}` blocks to adopt existing infrastructure; confirm the plan shows no replacements.

## Verify
`terraform fmt -check`, `terraform validate`, `tflint`/`checkov` if available, then a plan with zero unexpected changes.
