---
name: kubernetes
description: Write and debug Kubernetes manifests and Helm charts - deployments, services, probes, resources, config and secrets, rollouts - and troubleshoot failing pods. Use for k8s YAML, kubectl debugging and cluster deployment questions.
---
# Kubernetes

## Manifests that behave
- Deployment: `replicas`, labels/selectors that match, `strategy.rollingUpdate` with sensible `maxUnavailable`/`maxSurge`.
- Always set `resources.requests` (and memory `limits`); CPU limits only when needed (throttling).
- `readinessProbe` (traffic) and `livenessProbe` (restart) that hit cheap endpoints; `startupProbe` for slow boots.
- `securityContext`: `runAsNonRoot: true`, `readOnlyRootFilesystem: true`, drop capabilities.
- Config in ConfigMaps, credentials in Secrets (or an external secret manager); never bake secrets into images.
- `PodDisruptionBudget` and anti-affinity/topology spread for anything with replicas > 1.
- Pin image tags or digests; never `:latest` in production.

## Debugging a broken workload
```sh
kubectl get pods -n NS -o wide
kubectl describe pod POD -n NS          # events: scheduling, image pulls, probe failures, OOMKilled
kubectl logs POD -n NS --previous       # the crash before the restart
kubectl get events -n NS --sort-by=.lastTimestamp
kubectl exec -it POD -n NS -- sh        # inspect from inside
kubectl port-forward svc/NAME 8080:80 -n NS
```
Common causes: `ImagePullBackOff` (name/tag/registry auth), `CrashLoopBackOff` (app error; read `--previous` logs), `Pending` (resources, node selectors, PVC), `OOMKilled` (raise memory or fix leak), readiness failing (wrong port/path), Service selector not matching pod labels.

## Changes
Validate with `kubectl apply --dry-run=server -f` or `kubeconform`; diff with `kubectl diff -f`; roll out with `kubectl rollout status` and know `kubectl rollout undo`.
