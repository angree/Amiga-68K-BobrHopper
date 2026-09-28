// The one place the R36S / PC build's version number lives. The title screen shows it and
// tools/package_r36s.ps1 names the package after it, so the two can never disagree.
//
// It exists because they did disagree once: reports about sounds came in against v018, v019 and v022 while the
// fixes were already in a later build, and nothing on screen said which was running (docs/PROGRESS.md, O18/O19).
// The SF2000 core has carried kCoreVersion for exactly this reason since v003.
#pragma once

namespace cr {

const char *const kAppVersion = "v027";

} // namespace cr
