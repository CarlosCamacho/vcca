// Pass/fail bookkeeping shared by the page-driving tests: check() prints one
// line per assertion; done() adds any page errors, closes the browser and sets
// the exit code (the number of failures), which test/run.sh reads.
let fails = 0;
export function check(ok, what) {
	console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`);
	if (!ok) fails++;
	return ok;
}
export async function done(logs, browser) {
	for (const l of logs || []) check(false, `page error: ${l}`);
	if (browser) await browser.close();
	console.log(fails ? `FAIL: ${fails} checks failed` : 'no errors');
	process.exit(fails ? 1 : 0);
}
