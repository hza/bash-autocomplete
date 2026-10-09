// Completion helper for specs/duti.yaml.
//
//   osascript -l JavaScript duti.js MODE [BUNDLE_ID]
//
// Modes:
//   apps         "bundle_id<TAB>App name" for every installed app (for `duti -s`)
//   utis         "uti<TAB>description (.ext ...)" for every UTI known to the system
//                (for `duti -d` / `duti -l`)
//   exts         "ext<TAB>uti → App" for every file extension: its UTI and the app that
//                opens it now (for `duti -x`; also handy as a table: duti.js exts | column -t -s $'\t')
//   args BUNDLE [WORD]
//                UTIs and URL schemes for `duti -s BUNDLE WORD<TAB>`, described as
//                ".ext ... → app that opens it now" (the type's name when it has no extension):
//                ★ = declared by the app in its Info.plist,
//                ✔ = currently opens with the app (asked from LaunchServices).
//                Only entries starting with WORD, plus UTIs with WORD as an extension
//                (`md` -> net.daringfireball.markdown); the spec sets `filter: false`.
//
// Everything is read in this one process (NSBundle, LaunchServices), which is
// what makes it fast enough to run on every TAB without a cache (~0.3-0.6 s).

ObjC.import("CoreServices");

// Folders scanned for apps. /Applications is also scanned one level deep
// (e.g. /Applications/Utilities, vendor folders).
var APP_DIRS = [
  "/Applications",
  $.NSHomeDirectory().js + "/Applications",
  "/System/Applications",
  "/System/Applications/Utilities",
  "/System/Library/CoreServices",
  "/System/Library/CoreServices/Applications",
];

// Declares the system's own UTIs (public.plain-text, com.adobe.pdf, ...).
var CORE_TYPES = "/System/Library/CoreServices/CoreTypes.bundle";

var fm = $.NSFileManager.defaultManager;

// Names in a directory, or [] if it can't be read.
function listDir(dir) {
  var list = fm.contentsOfDirectoryAtPathError(dir, null);
  return list.isNil() ? [] : list.js.map(function (name) { return name.js; });
}

// A CFStringRef returned by a CoreServices call, as a JS string ("" for NULL).
function cfString(ref) {
  return ref ? ObjC.castRefToObject(ref).js || "" : "";
}

// "Name (Role)", "Name", "Role" or "" from a document/URL type entry.
function nameAndRole(name, role) {
  return name && role ? name + " (" + role + ")" : name || role || "";
}

// Paths of all app bundles, plus CoreTypes.bundle first.
function bundlePaths() {
  var paths = [CORE_TYPES];
  APP_DIRS.forEach(function (dir) {
    listDir(dir).forEach(function (name) {
      if (/\.app$/.test(name)) {
        paths.push(dir + "/" + name);
      } else if (dir == "/Applications") {
        listDir(dir + "/" + name).forEach(function (sub) {
          if (/\.app$/.test(sub)) paths.push(dir + "/" + name + "/" + sub);
        });
      }
    });
  });
  return paths;
}

// The Info.plist of a bundle as a plain JS object, or null.
function readInfo(path) {
  var bundle = $.NSBundle.bundleWithPath(path);
  if (bundle.isNil() || bundle.infoDictionary.isNil()) return null;
  try {
    return ObjC.deepUnwrap(bundle.infoDictionary);
  } catch (e) {
    return null; // values deepUnwrap can't convert; skip the bundle
  }
}

function run(argv) {
  var mode = argv[0];
  var bundleId = (argv[1] || "").toLowerCase(); // only for "args"
  var word = argv[2] || "";                      // only for "args"
  var wordExt = "." + word.toLowerCase();
  var out = [];
  var types = {};    // uti -> {desc, exts: [".md", ...]}
  var own = {};      // uti or scheme declared by bundleId -> "Name (Role)"
  var opens = {};    // uti or scheme that currently opens with bundleId
  var openCount = 0; // number of UTIs in `opens`
  var exts = {};     // file extension -> uti
  var appNames = {}; // bundle id (lowercase) -> app name
  var extsOf = {};   // uti -> [".md", ...], for matching WORD as an extension

  bundlePaths().forEach(function (path) {
    var info = readInfo(path);
    if (!info) return;
    var id = info.CFBundleIdentifier || "";
    var isTarget = id.toLowerCase() == bundleId;
    if (id && /\.app$/.test(path)) appNames[id.toLowerCase()] = path.replace(/.*\//, "").replace(/\.app$/, "");

    if (mode == "apps") {
      if (id && /\.app$/.test(path)) out.push(id + "\t" + path.replace(/.*\//, "").replace(/\.app$/, ""));
      return;
    }

    // UTIs defined by this bundle, with description and file extensions.
    var decls = [].concat(info.UTExportedTypeDeclarations || [], info.UTImportedTypeDeclarations || []);
    decls.forEach(function (decl) {
      var uti = decl.UTTypeIdentifier;
      if (!uti || types[uti]) return; // first definition wins
      var ext = [].concat((decl.UTTypeTagSpecification || {})["public.filename-extension"] || []);
      ext.forEach(function (e) { e = e.toLowerCase(); if (!(e in exts)) exts[e] = uti; });
      types[uti] = {desc: decl.UTTypeDescription || "", exts: ext.map(function (e) { return "." + e; })};
    });

    // UTIs the app can open (may be defined elsewhere, or nowhere).
    (info.CFBundleDocumentTypes || []).forEach(function (doc) {
      var desc = nameAndRole(doc.CFBundleTypeName, doc.CFBundleTypeRole);
      // Apps may list bare extensions (VS Code does); the system maps them to a UTI.
      if (mode == "exts") (doc.CFBundleTypeExtensions || []).forEach(function (e) {
        e = String(e).toLowerCase();
        if (e == "*" || e in exts) return;
        exts[e] = cfString($.UTTypeCreatePreferredIdentifierForTag($("public.filename-extension"), $(e), null));
      });
      (doc.LSItemContentTypes || []).forEach(function (uti) {
        if (!(uti in types)) types[uti] = {desc: "", exts: []};
        if (isTarget && !(uti in own)) own[uti] = desc;
      });
    });

    // URL schemes, only needed for the target app.
    if (isTarget) {
      (info.CFBundleURLTypes || []).forEach(function (url) {
        var desc = nameAndRole(url.CFBundleURLName, url.CFBundleTypeRole);
        (url.CFBundleURLSchemes || []).forEach(function (scheme) { own[scheme] = desc; });
      });
    }
  });

  if (mode == "apps") return out.join("\n");

  // Name of the app that opens a UTI (or URL scheme) now, "" if none.
  function currentApp(uti, isScheme) {
    var id = cfString(isScheme ? $.LSCopyDefaultHandlerForURLScheme($(uti))
                               : $.LSCopyDefaultRoleHandlerForContentType($(uti), $.kLSRolesAll)).toLowerCase();
    return appNames[id] || id;
  }

  if (mode == "exts") {
    Object.keys(exts).sort().forEach(function (e) {
      var app = currentApp(exts[e]);
      out.push(e + "\t" + exts[e] + (app ? " → " + app : ""));
    });
    return out.join("\n");
  }

  // Final descriptions. "utis": "name (.ext ...)"; "args": ".ext ... → current app",
  // and which UTIs open with the target app.
  for (var uti in types) {
    if (/^public\.app-category/.test(uti)) { // App Store categories, not file types
      delete types[uti];
      continue;
    }
    var type = types[uti];
    var name = type.desc || cfString($.UTTypeCopyDescription($(uti))); // system's localized name
    if (mode == "utis") {
      types[uti] = [name, type.exts.length ? "(" + type.exts.join(" ") + ")" : ""].filter(Boolean).join(" ");
      continue;
    }
    extsOf[uti] = type.exts;
    var app = currentApp(uti);
    if (app && app == appNames[bundleId] || app == bundleId) {
      opens[uti] = true;
      openCount++;
    }
    types[uti] = (type.exts.length ? type.exts.join(" ") : name) + (app ? " → " + app : "");
  }

  if (mode == "utis") {
    for (uti in types) out.push(types[uti] ? uti + "\t" + types[uti] : uti);
    return out.join("\n");
  }

  // Starts with WORD, or has WORD as one of its extensions.
  function matches(key) {
    return key.indexOf(word) == 0 || (extsOf[key] || []).some(function (e) { return e.toLowerCase() == wordExt; });
  }

  // The app's own entries first. URL schemes (no dot) are never folded by
  // `tree: "."`, so they also carry the "opens N types" summary.
  for (var key in own) {
    if (!matches(key)) continue;
    var isScheme = !/\./.test(key), text;
    if (isScheme) {
      var handler = currentApp(key, true);
      if (handler && handler == appNames[bundleId] || handler == bundleId) opens[key] = true;
      text = (own[key] || "URL scheme") + (handler ? " → " + handler : "") +
             (openCount ? " · opens " + openCount + " types (✔)" : "");
    } else {
      text = types[key] || own[key];
    }
    out.push(key + "\t★" + (opens[key] ? "✔ " : " ") + text);
  }

  // Every other UTI.
  for (uti in types) {
    if (uti in own || !matches(uti)) continue;
    out.push(uti + "\t" + (opens[uti] ? "✔ " : "") + types[uti]);
  }
  return out.join("\n");
}
