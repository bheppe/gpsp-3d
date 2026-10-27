# Universal-Updater feed (UniStore v3) from the GitHub release lists of
# gpSP 3D (with Icon Maker) and Snes9x 3D.
#
# Input: {gpsp: [releases], snes9x: [releases]}, each release
#        {tag_name, draft, prerelease, published_at, body,
#         assets: [{name, size, browser_download_url}]}.
# Arg:   $base — site URL, no trailing slash.
#
# storeInfo.revision is left at 0: Universal-Updater only re-downloads a store
# whose revision is strictly greater than the one it holds, so the number has
# to come from the feed already deployed, not from the releases (see pages.yml).
#
# Run locally:
#   jq -S --arg base https://bheppe.github.io/gpsp-3d -f .github/unistore.jq releases.json

# Same format Universal-DB uses. Universal-Updater compares it as a string
# against the one recorded at install time to flag an update, so it must sort.
def stamp: fromdateiso8601 | strftime("%Y-%m-%d at %H:%M:%S (UTC)");
def mib: (. / 1048576 * 10 | round / 10 | tostring) + " MiB";
def pre($r): if $r.prerelease then {type: "prerelease"} else {} end;
def asset($r; $name): $r.assets[] | select(.name == $name);
def cia($r; $m): asset($r; $m.cia) as $a | {
  script: [
    {type: "downloadFile", file: $a.browser_download_url, output: "/\($m.cia)"},
    {type: "installCia", file: "/\($m.cia)"},
    {type: "deleteFile", file: "/\($m.cia)"}
  ], size: ($a.size | mib) } + pre($r);
# "%3DSX%/<name>.3dsx" and nothing deeper: Universal-Updater rewrites it with a
# greedy regex into /3ds/<name>.3dsx or, with its "3DSX in folder" option,
# /3ds/<name>/<name>.3dsx. A subfolder here would be doubled by that option.
def tdsx($r; $m): asset($r; $m.tdsx) as $a | {
  script: [
    {type: "downloadFile", file: $a.browser_download_url, output: "%3DSX%/\($m.tdsx)"}
  ], size: ($a.size | mib) } + pre($r);

# One storeContent entry: the latest release's downloads under their plain
# names, older ones prefixed with their tag. Nothing when the app has no
# release with its files yet. Sorted by date, since Snes9x 3D's tags
# (v1.61-layers6) aren't version numbers.
def app($rels; $m):
  [ $rels[] | select(.draft | not)
            | select(.tag_name != "nightly")
            | select(any(.assets[]; .name == $m.cia or .name == $m.tdsx)) ]
  | sort_by(.published_at) | reverse
  | select(length > 0)
  | . as $rs
  | ([ $rs[] | select(.prerelease | not) ][0] // $rs[0]) as $latest
  | { info: {
        title: $m.title, author: $m.author, description: $m.description,
        version: $latest.tag_name,
        console: ["3DS"], category: [$m.category], license: $m.license,
        icon_index: $m.icon_index, sheet_index: 0,
        last_updated: ($latest.published_at | stamp),
        # GitHub stores notes with CRLF; the 3DS font draws the CR. The install
        # QR codes that release.yml appends are no use on the console.
        releasenotes: (($latest.body // "")
          | gsub("\r"; "")
          | sub("\\s*<!-- gpsp3d-qr:start -->[\\s\\S]*$"; "")),
        # The CIA's unique ID: marks the app as installed
        title_ids: [$m.title_id],
        installed_files: ["%3DSX%/\($m.tdsx)"]
      } }
    + ( [ $rs[] as $r
          | (if $r.tag_name == $latest.tag_name then "" else "[\($r.tag_name)] " end) as $p
          # A release missing one of the files just offers the other
          | {("\($p)\($m.cia)"): cia($r; $m)}, {("\($p)\($m.tdsx)"): tdsx($r; $m)} ]
        | add );

. as $in
| [ app($in.gpsp; {
      title: "gpSP 3D", author: "bheppe",
      description: "Game Boy Advance games in stereoscopic 3D",
      category: "emulator", license: "gpl-2.0", icon_index: 0,
      # UNIQUE_ID 0xBAC81 from gpsp3ds/build-gpsp3ds.sh
      title_id: 765057,
      cia: "gpsp3d.cia", tdsx: "gpsp3d.3dsx" }),
    app($in.gpsp; {
      title: "Icon Maker", author: "bheppe",
      description: "HOME Menu icons for GBA and SNES games",
      category: "utility", license: "gpl-2.0", icon_index: 1,
      # UNIQUE_ID 0xF3D01 from iconmaker/build-iconmaker.sh
      title_id: 998657,
      cia: "iconmaker.cia", tdsx: "iconmaker.3dsx" }),
    # Snes9x 3D also attaches Icon Maker; it is listed once, from gpSP 3D.
    app($in.snes9x; {
      title: "Snes9x 3D", author: "bheppe, matbo87, bubble2k16",
      description: "SNES games in stereoscopic 3D",
      category: "emulator", license: "other", icon_index: 2,
      # APP_UNIQUE_ID 0x38A71 from snes9x_3ds resources/AppInfo
      title_id: 232049,
      cia: "snes9x_3ds.cia", tdsx: "snes9x_3ds_layers.3dsx" }) ]
| if any(.[]; .info.title == "gpSP 3D") then . else error("no gpSP 3D release with its files") end
| {
  storeInfo: {
    title: "gpSP 3D", author: "bheppe",
    description: "GBA and SNES games in stereoscopic 3D",
    file: "gpsp3d.unistore",
    url: "\($base)/unistore/gpsp3d.unistore",
    infoURL: "\($base)/unistore/gpsp3d-info.json",
    sheet: "gpsp3d.t3x", sheetURL: "\($base)/unistore/gpsp3d.t3x",
    version: 3, revision: 0
  },
  storeContent: .
}
