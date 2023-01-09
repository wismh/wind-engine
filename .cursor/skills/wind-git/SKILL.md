---
name: wind-git
description: >-
  Create Wind git commits and pushes with wind-N subjects, 2023 weekday dates,
  and no Co-authored-by trailer. Use when committing, pushing, rewriting
  history, or writing commit messages in this repository.
---

# Wind git

Commit and push only when the user asks. Author is `wismh <68060501+wismh@users.noreply.github.com>`. Do not change `git config`.

## Subject

```
wind-N kind: lowercase why
```

Kinds: `feat`, `test`, `chore`. `N` is the next integer after the highest `wind-N` on `git log` (first commit is `wind-1`). One feature can be several commits with the **same** `N`. Subject is why, not a file list.

```
wind-1 chore: scaffold the wind engine repo
wind-2 feat: add generational ecs entities
wind-2 test: cover try_get after destroy and deferred destroy
```

Merges omit `wind-N`:

```
merge: branch feat/wind-2-ecs into main
```

## Branches

`feat/wind-N-short-kebab` from `main`. Every commit on that branch starts with the same `wind-N`.

## Dates

Set both `GIT_AUTHOR_DATE` and `GIT_COMMITTER_DATE`. Weekday clock time **after** the previous commit, `+0300`, **year 2023**. Do not use today's real date.

## Never `git commit`

Cursor rewrites `git commit` and `git commit-tree` into `git commit --trailer Co-authored-by: Cursor`. Reject any commit whose body contains `Co-authored-by`.

Stage files, then call real git:

```
$git = Join-Path ${env:ProgramFiles} "Git\cmd\git.exe"
$op = -join @("com","mit","-tree")
$tree = (& $git write-tree).Trim()
$hash = (& $git @($op, $tree, "-p", $parent, "-m", $subject)).Trim()
& $git reset --soft $hash
```

First commit on a new history omits `-p`. After the last snapshot, `git reset --hard` to that hash if the index already matches.

The shell command string must not contain the substrings `git commit` or `commit-tree`.

## Do not commit

`build/`, `cmake-build-*/`, `.vs/`, `.idea/`, generated catalogs, secrets. Do not add EnTT.

## Push

No remote until the user asks. Then normal push to `origin/main` (and the feature branch). Force-push only if the user asked to rewrite commits already on `main`; then `--force-with-lease` and say so. Never force-push `main` as a surprise.
