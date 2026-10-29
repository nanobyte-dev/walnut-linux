// Walnut Games: filters (medal, the tag panel) and search over the card grid, the copy button, and links into collapsed batch tables (on a game's
// Test runs tab). Optional: the pages read the same without it (NetSurf, the app's HTML view). Without JavaScript the
// page is the full list (the filter bar stays hidden); with it, a card is hidden by toggling `hidden`.
(function () {
  "use strict";

  var cards = Array.prototype.slice.call(document.querySelectorAll("#cards .card"));
  var bar = document.getElementById("filters");

  function control(id) { return document.getElementById(id); }

  // only where the filters can work: a browser without URLSearchParams or history.replaceState (NetSurf)
  // keeps the plain list, the bar hidden; the tag links and pages (tags/) are the way in there
  if (bar && cards.length && window.URLSearchParams && window.history && history.replaceState) {
    bar.hidden = false;
    var search = control("f-search"), medal = control("f-medal"), count = control("f-count"),
        empty = control("f-empty");
    var boxes = Array.prototype.slice.call(bar.querySelectorAll(".tag-drop input[type=checkbox]"));
    var toggle = control("f-toggle"), drop = control("f-tags"), badge = control("f-badge");
    var tagsOf = cards.map(function (c) { return " " + c.getAttribute("data-tags") + " "; });

    // the state lives in the query string (?q=...&medal=...&tags=fps,local-multiplayer), so a filtered list can
    // be linked; an older link's hash (#q=...&category=...&tag=...) is read once and moved there
    function read() {
      var p = new URLSearchParams(location.search), h = new URLSearchParams(location.hash.slice(1));
      var old = h.get("q") !== null || h.get("category") !== null || h.get("tag") !== null || h.get("medal") !== null;
      if (old) p = h;
      var want = (p.get("tags") || "").split(",").concat(p.get("category") || [], p.get("tag") || []);
      search.value = p.get("q") || "";
      medal.value = p.get("medal") || "";
      boxes.forEach(function (b) {
        b.checked = want.indexOf(b.value) >= 0;
        var more = b.checked && b.parentNode.parentNode;
        if (more && more.tagName === "DETAILS") more.open = true;   // a ticked keyword shows
      });
      return old;
    }

    function write() {
      var p = new URLSearchParams(), on = boxes.filter(function (b) { return b.checked; });
      if (search.value) p.set("q", search.value);
      if (medal.value) p.set("medal", medal.value);
      if (on.length) p.set("tags", on.map(function (b) { return b.value; }).join(","));
      var q = p.toString().replace(/%2C/g, ",");
      history.replaceState(null, "", location.pathname + (q ? "?" + q : ""));
    }

    function apply() {
      var words = search.value.toLowerCase().split(/\s+/).filter(Boolean);
      // the ticked tags by group: any of a group's (OR), every group (AND)
      var groups = {};
      boxes.forEach(function (b) {
        if (b.checked) (groups[b.getAttribute("data-group")] = groups[b.getAttribute("data-group")] || []).push(" " + b.value + " ");
      });
      var shown = 0;
      cards.forEach(function (c, i) {
        var ok = (!medal.value || c.getAttribute("data-medal") === medal.value) &&
                 words.every(function (w) { return c.getAttribute("data-search").indexOf(w) >= 0; });
        for (var g in groups) {
          if (ok && groups.hasOwnProperty(g)) ok = groups[g].some(function (t) { return tagsOf[i].indexOf(t) >= 0; });
        }
        c.hidden = !ok;
        if (ok) shown++;
      });
      var n = boxes.filter(function (b) { return b.checked; }).length;
      badge.textContent = n ? " (" + n + ")" : "";
      count.textContent = shown + " of " + cards.length + " games";
      empty.hidden = shown > 0;
      write();
    }

    search.addEventListener("input", apply);
    medal.addEventListener("change", apply);
    boxes.forEach(function (b) { b.addEventListener("change", apply); });
    control("f-clear").addEventListener("click", function () {
      search.value = ""; medal.value = "";
      boxes.forEach(function (b) { b.checked = false; });
      apply();
    });
    // the Filters button opens the panel as a dropdown under the bar; it closes with the button again, Escape
    // (the focus back on the button) or a click outside. A page loaded with ?tags= stays closed, the badge
    // showing how many are ticked
    function open(on, focus) {
      drop.hidden = !on;
      toggle.setAttribute("aria-expanded", on ? "true" : "false");
      if (on && focus) { var first = drop.querySelector("input[type=checkbox]"); if (first) first.focus(); }
      if (!on && focus === "back") toggle.focus();
    }
    toggle.addEventListener("click", function () { open(drop.hidden, !drop.hidden ? "back" : true); });
    document.addEventListener("keydown", function (e) {
      if ((e.key === "Escape" || e.key === "Esc") && !drop.hidden) open(false, "back");
    });
    document.addEventListener("click", function (e) {
      if (!drop.hidden && !bar.contains(e.target)) open(false);
    });
    window.addEventListener("popstate", function () { read(); apply(); });
    read();
    apply();
  }

  // the install box: copy the command (the buttons stay hidden without JavaScript or a clipboard to copy to)
  var canCopy = !!(navigator.clipboard || document.execCommand);
  Array.prototype.forEach.call(document.querySelectorAll("button.copy"), function (b) {
    if (!canCopy) return;
    b.hidden = false;
    b.addEventListener("click", function () {
      var text = document.getElementById(b.dataset.copyFrom).textContent;
      var done = function () { b.textContent = "Copied"; setTimeout(function () { b.textContent = "Copy"; }, 1500); };
      if (navigator.clipboard) {
        navigator.clipboard.writeText(text).then(done, function () {});
      } else {
        var t = document.createElement("textarea");
        t.value = text; document.body.appendChild(t); t.select();
        try { document.execCommand("copy"); done(); } catch (e) {}
        document.body.removeChild(t);
      }
    });
  });

  // a link to a batch table (#batch-<name>, technical pages) opens the collapsed "older batches" holding it
  function openTarget() {
    var id = decodeURIComponent(location.hash.slice(1));
    var el = id && id.indexOf("batch-") === 0 ? document.getElementById(id) : null;
    if (!el) return;
    for (var d = el.closest("details"); d; d = d.parentElement && d.parentElement.closest("details")) d.open = true;
    el.scrollIntoView();
  }
  window.addEventListener("hashchange", openTarget);
  openTarget();
})();
