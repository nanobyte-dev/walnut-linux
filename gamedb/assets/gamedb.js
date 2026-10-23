// Walnut Games: filters and search over the card grid, and the copy button. Without JavaScript the
// page is the full list (the filter bar stays hidden); with it, a card is hidden by toggling `hidden`.
(function () {
  "use strict";

  var cards = Array.prototype.slice.call(document.querySelectorAll("#cards .card"));
  var bar = document.getElementById("filters");

  function control(id) { return document.getElementById(id); }

  if (bar && cards.length) {
    bar.hidden = false;
    var search = control("f-search"), category = control("f-category"), medal = control("f-medal"),
        tag = control("f-tag"), count = control("f-count"),
        empty = control("f-empty");

    // the filter state lives in the URL hash (#q=...&category=...), so a filtered list can be linked
    function readHash() {
      var p = new URLSearchParams(location.hash.slice(1));
      search.value = p.get("q") || "";
      category.value = p.get("category") || "";
      medal.value = p.get("medal") || "";
      tag.value = p.get("tag") || "";
    }

    function writeHash() {
      var p = new URLSearchParams();
      if (search.value) p.set("q", search.value);
      if (category.value) p.set("category", category.value);
      if (medal.value) p.set("medal", medal.value);
      if (tag.value) p.set("tag", tag.value);
      var h = p.toString();
      history.replaceState(null, "", h ? "#" + h : location.pathname + location.search);
    }

    function apply() {
      var words = search.value.toLowerCase().split(/\s+/).filter(Boolean);
      var shown = 0;
      cards.forEach(function (c) {
        var d = c.dataset;
        var ok = (!category.value || d.category.split(" ").indexOf(category.value) >= 0) &&
                 (!medal.value || d.medal === medal.value) &&
                 (!tag.value || d.tags.split("|").indexOf(tag.value) >= 0) &&
                 words.every(function (w) { return d.search.indexOf(w) >= 0; });
        c.hidden = !ok;
        if (ok) shown++;
      });
      count.textContent = shown + " of " + cards.length + " games";
      empty.hidden = shown > 0;
      writeHash();
    }

    [search, category, medal, tag].forEach(function (el) {
      el.addEventListener(el === search ? "input" : "change", apply);
    });
    window.addEventListener("hashchange", function () { readHash(); apply(); });
    readHash();
    apply();
  }

  // the install box: copy the command
  document.querySelectorAll("button.copy").forEach(function (b) {
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
})();
