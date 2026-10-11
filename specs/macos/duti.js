// Completion helper for specs/duti.yaml.
//
//   osascript -l JavaScript duti.js MODE [BUNDLE_ID]
//
// Modes:
//   apps [WORD]  "bundle_id<TAB>App name" for every installed app (for `duti -s`) matching
//                WORD, ignoring case: start of the bundle id or of any of its dot parts
//                (`VSCode` -> com.microsoft.VSCode), or start of the app's file name or
//                CFBundleName / CFBundleDisplayName (`Preview`, `Code`); the spec sets
//                `filter: false`.
//   utis [WORD]  "uti<TAB>description (.ext ...)" for every UTI known to the system
//                (for `duti -d` / `duti -l`); WORD is matched like in "args" below
//   exts         "ext<TAB>uti → App" for every file extension: its UTI and the app that
//                opens it now (for `duti -x`; also handy as a table: duti.js exts | column -t -s $'\t')
//   args BUNDLE [WORD]
//                UTIs and URL schemes for `duti -s BUNDLE WORD<TAB>`, described as
//                ".ext ... → app that opens it now" (the type's name when it has no extension):
//                ★ = declared by the app in its Info.plist,
//                ✔ = currently opens with the app (asked from LaunchServices).
//                Only entries starting with WORD, plus UTIs with WORD as an extension
//                (`md` -> net.daringfireball.markdown); the spec sets `filter: false`.
//                Several UTIs for one extension are listed numbered, "ts2: public.typescript"
//                (display only; U+00A0 after the colon), and `ts2` then completes to that
//                UTI ("mp4_2" for extensions ending in a digit).
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

// Several matches whose common prefix doesn't contain the word (`keychain` -> com.apple.X,
// com.apple.Y): bash would replace the word with that prefix ("com.apple."), losing what
// was typed. A display-only entry starting with "🔎" leaves no common prefix, so bash
// keeps the word and just lists the matches. A prefix that contains the word is fine to
// insert (`Preview` -> com.apple.Preview, then TAB lists Preview and PreviewShell).
function keepWord(out, word) {
  if (out.length < 2 || !word) return out;
  var keys = out.map(function (line) { return line.split("\t")[0]; });
  var prefix = keys.reduce(function (p, k) {
    var i = 0;
    while (i < p.length && p[i] == k[i]) i++;
    return p.slice(0, i);
  });
  if (prefix && prefix.toLowerCase().indexOf(word.toLowerCase()) < 0 && word.indexOf(prefix) != 0)
    out.push("🔎\u00a0" + word + "\t" + keys.length + " matches");
  return out;
}

function run(argv) {
  var mode = argv[0];
  var appWord = mode == "apps" ? (argv[1] || "").toLowerCase() : ""; // only for "apps"
  var bundleId = (argv[1] || "").toLowerCase(); // only for "args"
  var word = (mode == "utis" ? argv[1] : argv[2]) || ""; // typed word: "utis", "args"
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
      var appName = path.replace(/.*\//, "").replace(/\.app$/, "");
      var names = [id, appName, info.CFBundleName, info.CFBundleDisplayName].concat(id.split("."));
      var hit = names.some(function (n) { return n && String(n).toLowerCase().indexOf(appWord) == 0; });
      if (id && /\.app$/.test(path) && hit) out.push(id + "\t" + appName);
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

  if (mode == "apps") return keepWord(out, appWord).join("\n");

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
    extsOf[uti] = type.exts;
    if (mode == "utis") {
      types[uti] = [name, type.exts.length ? "(" + type.exts.join(" ") + ")" : ""].filter(Boolean).join(" ");
      continue;
    }
    var app = currentApp(uti);
    if (app && app == appNames[bundleId] || app == bundleId) {
      opens[uti] = true;
      openCount++;
    }
    types[uti] = (type.exts.length ? type.exts.join(" ") : name) + (app ? " → " + app : "");
  }

  // UTIs declaring extension `ext`, sorted like bash sorts the list.
  function utisForExt(ext) {
    ext = "." + ext.toLowerCase();
    return Object.keys(extsOf).filter(function (u) {
      return extsOf[u].some(function (e) { return e.toLowerCase() == ext; });
    }).sort();
  }

  // WORD as an extension; else "ts2" / "mp4_2" = the 2nd UTI for that extension
  // (`mp4` itself stays an extension).
  var byExt = word ? utisForExt(word) : [];
  var numbered = byExt.length > 1;
  var pick = !byExt.length && /^(.*\D)(\d+)$/.exec(word);
  if (pick) {
    var base = pick[1].replace(/_$/, ""), all = utisForExt(base), k = +pick[2];
    if (all.length > 1 && k >= 1 && k <= all.length) return all[k - 1];
  }

  // Starts with WORD, or has WORD as one of its extensions.
  function matches(key) {
    return key.indexOf(word) == 0 || byExt.indexOf(key) >= 0;
  }

  // List name of an entry: "ts2:<U+00A0>uti" for a numbered extension match, else the key.
  // All numbered names start with WORD, so bash keeps the word and only lists them.
  function label(key) {
    var i = numbered ? byExt.indexOf(key) : -1;
    return i < 0 ? key : word + (/\d$/.test(word) ? "_" : "") + (i + 1) + ":\u00a0" + key;
  }

  if (mode == "utis") {
    for (uti in types) if (matches(uti)) out.push(types[uti] ? label(uti) + "\t" + types[uti] : label(uti));
    return keepWord(out, word).join("\n");
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
    out.push(label(key) + "\t★" + (opens[key] ? "✔ " : " ") + text);
  }

  // Nothing typed yet: a display-only hint that an extension finds its UTI.
  if (!word) out.push("💡\u00a0type\u00a0an\u00a0extension\tmd<TAB> → net.daringfireball.markdown, pdf<TAB> → com.adobe.pdf");

  // Every other UTI.
  for (uti in types) {
    if (uti in own || !matches(uti)) continue;
    out.push(label(uti) + "\t" + (opens[uti] ? "✔ " : "") + types[uti]);
  }
  return keepWord(out, word).join("\n");
}
