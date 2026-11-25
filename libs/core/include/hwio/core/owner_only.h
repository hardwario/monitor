#pragma once

// ADR-0186 (plan/features/0025, review M-4a), ADR-0426 (plan/features/0060 A4): files that may
// contain device secrets (command history, console files, application copies, "Save view…") are
// readable and writable by their owner only. Unix: mode 0600, set right after the file is opened
// and before the first write (an existing file is tightened when it is opened again; for a
// QSaveFile the temporary file that replaces the target). Windows: no code; the file keeps the ACL
// it inherits from its folder. For the history, console files and application copies that is the
// per-user application data folder (owner-only); a "Save view…" file goes to the folder the user
// chooses (Documents by default, which is per-user) and gets that folder's ACL.

class QFileDevice;

namespace hwio {

// Sets owner read/write only on Unix; a failure is logged (the write goes on). No-op on Windows.
void restrictToOwner(QFileDevice& file);

} // namespace hwio
