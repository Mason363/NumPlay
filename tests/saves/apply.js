/* Changes a list of files the way NumPlay Studio's Saves does (customize/saves.js), for tests/test_saves.py, which
 * then starts the launcher on the result in the emulator.
 *
 * node apply.js IN OUT JOB: IN and OUT are lists (the bytes of the calculator's files), JOB a JSON file:
 * { delete: [ids] }                  the saves of those games go, from the list and from NumPlay's copy
 * { restore: BACKUP }                everything a backup (a list too) holds is put back, game by game
 * with "games": games/games.json. Prints what it did as JSON: { writes, moves, failed }. */
'use strict';
const fs = require('fs'), path = require('path');
const S = require(path.join(__dirname, '..', '..', 'customize', 'saves.js'));
const [inp, out, jobFile] = process.argv.slice(2), job = JSON.parse(fs.readFileSync(jobFile, 'utf8'));
const before = new Uint8Array(fs.readFileSync(inp)), who = S.owners(job.games);
let groups = [];
if (job.delete) groups = job.delete.map((id) => S.takeOff(before, id, who));
else {
  const theirs = S.holds(S.keep(new Uint8Array(fs.readFileSync(job.restore))), who);
  groups = [...theirs.keys()].map((id) => S.putBack(before, theirs, id, who));
}
const res = S.put(before, groups), plan = S.plan(before, res.after);
// (the writes, one after the other, as the calculator gets them)
const now = before.slice();
for (const w of plan.writes) now.set(w.bytes, w.at);
if (!S.same(now, res.after)) throw new Error("the writes don't give the change");
fs.writeFileSync(out, now);
console.log(JSON.stringify({ writes: plan.writes.length, moves: plan.moves, failed: res.failed }));
