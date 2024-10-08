/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

do_get_profile();

// Test that interrupted sanitizations are properly tracked.

add_task(async function () {
  const { Sanitizer } = ChromeUtils.importESModule(
    "resource:///modules/Sanitizer.sys.mjs"
  );

  Services.prefs.setBoolPref(Sanitizer.PREF_NEWTAB_SEGREGATION, false);

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(Sanitizer.PREF_SANITIZE_ON_SHUTDOWN);
    Services.prefs.clearUserPref(
      Sanitizer.PREF_SHUTDOWN_BRANCH + "cookiesAndStorage"
    );
    Services.prefs.clearUserPref(Sanitizer.PREF_NEWTAB_SEGREGATION);
  });
  Services.prefs.setBoolPref(Sanitizer.PREF_SANITIZE_ON_SHUTDOWN, true);
  Services.prefs.setBoolPref(
    Sanitizer.PREF_SHUTDOWN_BRANCH + "cookiesAndStorage",
    true
  );

  await Sanitizer.onStartup();
  Assert.ok(Sanitizer.shouldSanitizeOnShutdown, "Should sanitize on shutdown");

  let pendingSanitizations = JSON.parse(
    Services.prefs.getStringPref(Sanitizer.PREF_PENDING_SANITIZATIONS, "[]")
  );
  Assert.equal(
    pendingSanitizations.length,
    1,
    "Should have 1 pending sanitization"
  );
  Assert.equal(
    pendingSanitizations[0].id,
    "shutdown",
    "Should be the shutdown sanitization"
  );
  Assert.ok(
    pendingSanitizations[0].itemsToClear.includes("cookiesAndStorage"),
    "Pref has been setup"
  );
  Assert.ok(
    !pendingSanitizations[0].options.isShutdown,
    "Shutdown option is not present"
  );

  // Check the preference listeners.
  Services.prefs.setBoolPref(Sanitizer.PREF_SANITIZE_ON_SHUTDOWN, false);
  pendingSanitizations = JSON.parse(
    Services.prefs.getStringPref(Sanitizer.PREF_PENDING_SANITIZATIONS, "[]")
  );
  Assert.equal(
    pendingSanitizations.length,
    0,
    "Should not have pending sanitizations"
  );
  Assert.ok(
    !Sanitizer.shouldSanitizeOnShutdown,
    "Should not sanitize on shutdown"
  );
  Services.prefs.setBoolPref(Sanitizer.PREF_SANITIZE_ON_SHUTDOWN, true);
  pendingSanitizations = JSON.parse(
    Services.prefs.getStringPref(Sanitizer.PREF_PENDING_SANITIZATIONS, "[]")
  );
  Assert.equal(
    pendingSanitizations.length,
    1,
    "Should have 1 pending sanitization"
  );
  Assert.equal(
    pendingSanitizations[0].id,
    "shutdown",
    "Should be the shutdown sanitization"
  );

  Assert.ok(
    pendingSanitizations[0].itemsToClear.includes("cookiesAndStorage"),
    "Pending sanitizations should include cookiesAndStorage"
  );
  Services.prefs.setBoolPref(
    Sanitizer.PREF_SHUTDOWN_BRANCH + "cookiesAndStorage",
    false
  );
  pendingSanitizations = JSON.parse(
    Services.prefs.getStringPref(Sanitizer.PREF_PENDING_SANITIZATIONS, "[]")
  );
  Assert.equal(
    pendingSanitizations.length,
    1,
    "Should have 1 pending sanitization"
  );
  Assert.ok(
    !pendingSanitizations[0].itemsToClear.includes("cookiesAndStorage"),
    "Pending sanitizations should have been updated"
  );

  // Check a sanitization properly rebuilds the pref.
  await Sanitizer.sanitize(["cookiesAndStorage"]);
  pendingSanitizations = JSON.parse(
    Services.prefs.getStringPref(Sanitizer.PREF_PENDING_SANITIZATIONS, "[]")
  );
  Assert.equal(
    pendingSanitizations.length,
    1,
    "Should have 1 pending sanitization"
  );
  Assert.equal(
    pendingSanitizations[0].id,
    "shutdown",
    "Should be the shutdown sanitization"
  );

  // Startup should run the pending one and setup a new shutdown sanitization.
  Services.prefs.setBoolPref(
    Sanitizer.PREF_SHUTDOWN_BRANCH + "cookiesAndStorage",
    false
  );
  await Sanitizer.onStartup();
  pendingSanitizations = JSON.parse(
    Services.prefs.getStringPref(Sanitizer.PREF_PENDING_SANITIZATIONS, "[]")
  );
  Assert.equal(
    pendingSanitizations.length,
    1,
    "Should have 1 pending sanitization"
  );
  Assert.equal(
    pendingSanitizations[0].id,
    "shutdown",
    "Should be the shutdown sanitization"
  );
  Assert.ok(
    !pendingSanitizations[0].itemsToClear.includes("cookiesAndStorage"),
    "Pref has been setup"
  );
});
