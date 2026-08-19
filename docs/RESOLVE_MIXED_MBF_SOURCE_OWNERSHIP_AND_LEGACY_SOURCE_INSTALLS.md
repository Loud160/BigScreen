# Codex Third-Round Discussion — Resolve Mixed MBF/Source Ownership and Legacy Source Installs

## Objective

The logging architecture is now accepted.

Do **not** revisit the hybrid logging design unless this ownership analysis reveals a direct interaction with it.

Before implementation begins, resolve the remaining ambiguity around:

1. what source deployment should do when **ModsBeforeFriday already owns a Big Screen installation**;
2. what source removal should do if a source deployment has overwritten files that MBF believes it owns;
3. how the first version of the new receipt/removal system should safely handle **existing source deployments created before receipts existed**.

This is still an **analysis/design task only**.

Do not modify code, deployment scripts, Quest files, or MBF state.

---

# 1. Problem: MBF ownership + source overwrite

The current findings establish that:

- MBF determines Big Screen installation status primarily from the presence of declared destination files;
- direct source deployment places files at those same destinations;
- this can make MBF believe an imported QMOD is already installed without actually replacing the source-deployed files.

There is a second related case that must now be addressed.

Suppose Big Screen was originally installed through MBF:

```text
MBF package metadata exists
libbigscreen.so = MBF build A
```

Then the developer runs:

```text
Build-And-Deploy.bat
```

and source deployment replaces that binary with:

```text
libbigscreen.so = source build B
```

MBF metadata still says Big Screen is installed.

Later:

```text
Remove-BigScreen.bat
```

sees that MBF claims the path.

The previous removal recommendation was:

> Preserve MBF-owned files.

But in this case the file currently at that path is **not the MBF version anymore**. It is the source-deployed version.

Simply preserving it would therefore leave the source build active and reproduce the ownership confusion we are trying to eliminate.

Determine the safest policy.

---

# 2. Compare mixed-install policies

Evaluate at least these approaches.

## Option A — refuse source deployment over MBF

If MBF package metadata shows Big Screen installed:

```text
Big Screen is currently managed by ModsBeforeFriday.

Source deployment would overwrite MBF-managed files without updating
MBF's package state.

Please remove Big Screen through MBF before using source deployment.
```

Then abort without modifying anything.

Advantages may include:

- clean ownership;
- simplest removal;
- no backup requirements;
- no false MBF state.

Disadvantages may include:

- one extra step when switching from release builds to development builds.

Assess whether this is the safest default.

---

## Option B — allow overwrite with explicit warning

Permit:

```text
MBF build
↓
source build
```

but require explicit confirmation.

The receipt records:

```text
previousState = present
previousSha256 = MBF binary hash
previousOwner = MBF
```

Determine how uninstall would restore the MBF state afterward.

Merely knowing the previous hash is not sufficient to reconstruct the previous file.

Would this require backing up every replaced MBF-owned file?

If so, evaluate the complexity and safety of that approach.

---

## Option C — automatically back up MBF-owned Big Screen files

Before source deployment:

```text
MBF Big Screen files
↓
BigScreen/SourceInstall/Previous/
```

Then source deploys over them.

When source removal occurs:

```text
verify backup
↓
restore previous MBF-owned files
↓
remove source receipt
```

Evaluate:

- storage;
- manifest consistency;
- dependency/runtime files;
- whether restored files definitely correspond to the MBF package metadata;
- whether MBF could have changed state while the source build was active;
- whether this creates more complexity than it solves.

---

## Option D — another design

If there is a cleaner solution, propose it.

---

# 3. Recommend the default mixed-install policy

Choose one clear default.

I prefer safety and understandable package ownership over cleverness.

The normal developer workflow should never silently create:

```text
MBF metadata says version A
actual loaded binary is version B
```

without telling the user.

If refusing mixed ownership is the cleanest solution, say so.

The source-deployment workflow is intended to remain fast, but requiring the developer to uninstall the MBF version **once when switching into source-development mode** is acceptable if it significantly simplifies ownership.

---

# 4. Source deployment when a receipt already exists

Determine behavior when:

```text
source-install.json already exists
```

and the developer runs Build & Deploy again.

This is the normal development case.

The deployer should recognize:

```text
existing Big Screen source installation
↓
replace/update it
↓
update receipt
```

without asking the developer to uninstall on every build.

Determine how previous hashes should be updated.

Important question:

If the original source deployment replaced a previously absent file:

```text
previousState = absent
```

subsequent source deployments should not turn that into:

```text
previousState = source build A
```

because removal should ultimately return to the state that existed **before source-development mode began**.

Preserve the original pre-source state across repeated development deployments.

Example:

```text
Before development:
file absent

Deploy A:
previousState = absent

Deploy B:
previousState must remain absent

Deploy C:
previousState must remain absent

Remove:
file deleted
```

Likewise, if another safe previous state is intentionally supported, preserve that original state across subsequent source builds.

---

# 5. Legacy source install problem

The Quest currently has a source-deployed Big Screen installation created **before source-install receipts existed**.

Other developers may eventually be in the same situation when upgrading the repository.

The first version of the new removal system therefore cannot assume:

```text
receipt missing
=
installation is unknown and cannot be removed
```

without providing some transition path.

Determine the safest way to migrate an existing source deployment into the receipt system.

---

# 6. Evaluate legacy migration options

## Option A — redeploy once to adopt the installation

When the new Build & Deploy script runs and:

```text
receipt missing
```

but every expected installed Big Screen file exactly matches the locally built payload/hash:

```text
recognize existing source deployment
↓
create/adopt receipt
↓
continue deployment
```

The receipt must establish a conservative original state.

Determine whether the script can reliably know which files were absent before the legacy source install.

Usually it cannot.

Therefore decide what ownership semantics should be recorded.

---

## Option B — legacy removal using exact known paths

The remover knows the exact files that the historical source deployment workflow installed.

It could:

1. enumerate those exact paths;
2. identify Big Screen-exclusive files;
3. verify filenames/metadata/hashes where possible;
4. remove only files confidently belonging to Big Screen;
5. preserve all shared/user data.

Determine whether this is safe enough for one-time migration.

No recursive deletion of broad directories is acceptable.

---

## Option C — compare against current local build

If the installed binary/runtime file hashes match the local build artifacts:

```text
safe candidate for source ownership
```

If they do not:

```text
preserve and warn
```

Assess whether this is sufficient.

---

## Option D — require one manual cleanup for legacy installs

If safe ownership cannot be established:

```text
Legacy source installation detected.
Automatic ownership cannot be proven.

Use the guided legacy cleanup workflow once.
```

Determine what that workflow would be.

---

# 7. Legacy receipt creation must not invent history

A receipt created after deployment already occurred must not falsely claim:

```text
previousState = absent
```

unless that can actually be established.

Consider an explicit value such as:

```text
previousState = unknownLegacy
```

or another representation.

Removal behavior for that state should be conservative.

Design this carefully.

---

# 8. MBF detection

Determine exactly how the source scripts can reliably identify whether Big Screen is currently MBF-managed.

Prefer inspection of actual MBF package metadata rather than merely checking whether:

```text
libbigscreen.so exists
```

because a source installation also creates that file.

Determine:

- package metadata path;
- mod ID matching;
- relevant version information;
- whether MBF package metadata alone is enough;
- whether package metadata may remain after a partial/failed uninstall.

The preflight should distinguish:

```text
MBF-managed Big Screen
source-managed Big Screen
legacy source Big Screen
ambiguous/mixed Big Screen
not installed
```

---

# 9. Define an installation-state classifier

Recommend a deterministic classifier that the source scripts can use.

For example:

```text
NOT_INSTALLED

SOURCE_MANAGED
    valid source-install receipt
    installed hashes consistent

MBF_MANAGED
    MBF Big Screen package metadata exists
    no source receipt

LEGACY_SOURCE
    expected Big Screen files exist
    no MBF metadata
    no source receipt

MIXED_OR_AMBIGUOUS
    MBF metadata + source receipt
    contradictory hashes
    duplicate phase files
    other inconsistent state
```

Refine these states as needed.

For every state define what:

```text
Build-And-Deploy
Remove-BigScreen
```

should do.

---

# 10. Desired safe behavior matrix

Produce a table similar to:

| Existing state | Build & Deploy | Remove Big Screen |
|---|---|---|
| Not installed | Install + create receipt | Nothing to remove |
| Source managed | Update + preserve original ownership history | Remove verified source files |
| MBF managed | ? | Direct user to MBF |
| Legacy source | ? | ? |
| Mixed/ambiguous | ? | ? |

Resolve every row.

No case should silently guess ownership.

---

# 11. Transition from source development back to MBF

One of the main reasons for adding the removal utility is:

```text
developer/tester uses source build
↓
later wants normal MBF release
```

The intended workflow should become deterministic.

Ideally:

```text
Remove-BigScreen.bat
↓
source-owned executable/runtime files removed
↓
settings preserved unless separately requested
↓
videos/library/logs preserved
↓
script confirms source installation is gone
↓
user opens MBF
↓
MBF installs release normally
```

Verify that after source removal there are no remaining files that cause MBF to falsely classify Big Screen as already installed.

This is essential.

List every path whose continued existence could fool MBF's file-existence check.

Do **not** remove user data merely because it contains Big Screen data.

Only MBF-declared installation payload destinations matter.

---

# 12. Runtime files and MBF detection

The current QMOD declares a large number of runtime files.

Since MBF determines installation from declared file existence, determine whether removal must remove **all source-deployed files that overlap the QMOD's declared installation payload** before MBF will correctly consider Big Screen absent.

If even one declared file remains, determine whether MBF still reports the package as installed or whether it requires all files.

The previous analysis says MBF checks whether all declared destinations exist.

Confirm the exact implication for transition:

```text
source uninstall
↓
at least one required QMOD destination absent
↓
MBF no longer thinks Big Screen is already installed
```

Even so, source removal should remove every verified source-owned deployment artifact rather than intentionally relying on one missing file.

---

# 13. Settings and user data remain separate

Regardless of ownership model:

Never treat these as proof that Big Screen is installed:

```text
BigScreen/Videos
BigScreen/Thumbnails
BigScreen/Video Import
BigScreen/library.json
BigScreen/Logs
Big Screen settings
map-local videos
movement/choreography files
```

These may remain after uninstall by design.

MBF/package ownership classification must concern executable/runtime/package files only.

---

# 14. Receipt location and uninstall survival

The proposed receipt lives under:

```text
/sdcard/ModData/com.beatgames.beatsaber/BigScreen/SourceInstall/
```

Verify whether:

- MBF installation touches this path;
- MBF uninstall touches this path;
- normal source updates preserve it;
- user-data cleanup operations affect it.

The receipt should survive long enough to let `Remove-BigScreen.bat` identify the source installation, but it should itself be deleted after successful source removal.

If another location would be safer, explain why.

---

# 15. Partial receipt recovery

Retain the previous proposal:

```text
source-install.partial.json
```

during deployment.

Clarify what should happen if:

```text
Build & Deploy crashes halfway through
```

On the next deploy/removal run:

- inspect the partial receipt;
- compare previous/current/intended hashes;
- determine which files were actually installed;
- either safely resume or safely undo;
- never assume the complete intended file list was successfully copied.

Define the simplest robust algorithm.

---

# 16. Do not solve this by forcing deployment through MBF

The direct-copy developer workflow should remain unless this analysis discovers a fundamental blocker.

The purpose of this round is to make direct deployment **ownership-aware**, not to turn development deployment into a package-manager workflow.

---

# 17. Deliverable

Do not implement anything.

Return:

## Mixed-install problem

Explain the exact ownership hole in the previous design.

## Recommended MBF/source policy

Choose whether source deployment should:

```text
REFUSE OVER MBF
ALLOW WITH WARNING
BACK UP AND RESTORE MBF
OTHER
```

and justify it.

## Installation-state classifier

Define the states and how they are detected.

## Build & Deploy behavior matrix

State what happens for each installation state.

## Removal behavior matrix

State what happens for each installation state.

## Legacy migration strategy

Explain how existing pre-receipt source installations should transition safely.

## Repeated source deployment

Explain how the original pre-source state remains preserved across many development deployments.

## MBF transition guarantee

Explain what must be removed so that after `Remove-BigScreen.bat`, MBF will no longer falsely detect the source payload as an installed QMOD.

## Partial-install recovery

Explain how `.partial` receipts are handled.

## Final implementation readiness

Finish with one of:

```text
READY TO IMPLEMENT
READY TO IMPLEMENT WITH THE FOLLOWING FIXED POLICY: ...
ONE MORE ISSUE MUST BE RESOLVED: ...
```

---

# Important rules

- Do not change any code or scripts.
- Do not modify the connected Quest.
- Do not install or uninstall Big Screen.
- Preserve the accepted hybrid logging design.
- Do not silently mix MBF ownership and source ownership.
- Do not claim a historical pre-install state that cannot be proven.
- Never delete a file whose ownership is ambiguous.
- Never delete downloaded videos or user-created content.
- Prefer a simple refusal/warning over complicated backup logic unless backup/restore provides a clear practical advantage.
- Keep the fast direct-copy development workflow.
- The source removal workflow must leave the Quest in a state where MBF can subsequently install Big Screen normally.

The goal is to eliminate the final ownership ambiguity before implementation:

> **How should Big Screen safely transition among MBF-managed, source-managed, legacy-source, and mixed/ambiguous installations without ever deleting the wrong file or leaving MBF fooled by a stale source deployment?**
