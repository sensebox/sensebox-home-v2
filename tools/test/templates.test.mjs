// The config generator's built-in templates (site/templates.js) must stay equal to the config
// files they were copied from; those files are checked against the firmware by test_config.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";

const root = new URL("../../", import.meta.url);
const read = (path) => readFileSync(new URL(path, root), "utf8");
const templates = JSON.parse(JSON.stringify(vm.runInNewContext(read("site/templates.js") + "\nTEMPLATES;")));

test("templates exist and have unique ids", () => {
  assert.ok(templates.length > 0);
  assert.equal(new Set(templates.map((t) => t.id)).size, templates.length);
});

for (const t of templates) {
  test(`template ${t.id} equals ${t.source}`, () => {
    assert.deepEqual(t.config, JSON.parse(read(t.source)),
      `site/templates.js is out of date: copy ${t.source} into it again`);
  });
}
