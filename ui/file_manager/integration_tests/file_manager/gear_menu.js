// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

/**
 * Expected files shown in Downloads with hidden disabled
 *
 * @type {!Array<!TestEntryInfo>}
 */
const BASIC_LOCAL_ENTRY_SET_WITHOUT_HIDDEN = [
  ENTRIES.hello,
  ENTRIES.world,
  ENTRIES.desktop,
  ENTRIES.beautiful,
  ENTRIES.photos,
  ENTRIES.crdownload,
];

/**
 * Expected files shown in Downloads with hidden enabled
 *
 * @type {!Array<!TestEntryInfo>}
 */
const BASIC_LOCAL_ENTRY_SET_WITH_HIDDEN = [
  ENTRIES.hello,
  ENTRIES.world,
  ENTRIES.desktop,
  ENTRIES.beautiful,
  ENTRIES.photos,
  ENTRIES.crdownload,
  ENTRIES.hiddenFile,
];

/**
 * Expected files shown in Drive with hidden enabled
 *
 * @type {!Array<!TestEntryInfo>}
 */
const BASIC_DRIVE_ENTRY_SET_WITH_HIDDEN = [
  ENTRIES.hello,
  ENTRIES.world,
  ENTRIES.desktop,
  ENTRIES.beautiful,
  ENTRIES.photos,
  ENTRIES.unsupported,
  ENTRIES.testDocument,
  ENTRIES.testSharedDocument,
  ENTRIES.hiddenFile,
];

const BASIC_ANDROID_ENTRY_SET = [
  ENTRIES.directoryDocuments,
  ENTRIES.directoryMovies,
  ENTRIES.directoryMusic,
  ENTRIES.directoryPictures,
];

const BASIC_ANDROID_ENTRY_SET_WITH_HIDDEN = [
  ENTRIES.directoryDocuments,
  ENTRIES.directoryMovies,
  ENTRIES.directoryMusic,
  ENTRIES.directoryPictures,
  ENTRIES.hello,
  ENTRIES.world,
  ENTRIES.directoryA,
];

/**
 * Expected files shown in Drive with Google Docs disabled
 *
 * @type {!Array<!TestEntryInfo>}
 */
const BASIC_DRIVE_ENTRY_SET_WITHOUT_GDOCS = [
  ENTRIES.hello,
  ENTRIES.world,
  ENTRIES.desktop,
  ENTRIES.beautiful,
  ENTRIES.photos,
  ENTRIES.unsupported,
];

/**
 * Gets the common steps to toggle hidden files in the Files app
 * @param {!Array<!TestEntryInfo>} basicSet Files expected before showing hidden
 * @param {!Array<!TestEntryInfo>} hiddenEntrySet Files expected after showing
 * hidden
 */
function runHiddenFilesTest(appId, basicSet, hiddenEntrySet) {
  return runHiddenFilesTestWithMenuItem(
      appId, basicSet, hiddenEntrySet, '#gear-menu-toggle-hidden-files');
}

/**
 * Gets the common steps to toggle Android hidden files in the Files app
 * @param {!Array<!TestEntryInfo>} basicSet Files expected before showing hidden
 * @param {!Array<!TestEntryInfo>} hiddenEntrySet Files expected after showing
 * hidden
 */
function runAndroidHiddenFilesTest(appId, basicSet, hiddenEntrySet) {
  return runHiddenFilesTestWithMenuItem(
      appId, basicSet, hiddenEntrySet,
      '#gear-menu-toggle-hidden-android-folders');
}

/**
 * Gets the common steps to toggle hidden files in the Files app
 * @param {!Array<!TestEntryInfo>} basicSet Files expected before showing hidden
 * @param {!Array<!TestEntryInfo>} hiddenEntrySet Files expected after showing
 * hidden
 * @param {string} toggleMenuItemSelector Selector for the menu item that
 * toggles hidden file visibility
 */
async function runHiddenFilesTestWithMenuItem(
    appId, basicSet, hiddenEntrySet, toggleMenuItemSelector) {
  await remoteCall.waitForElement(appId, '#gear-button:not([hidden])');

  // Open the gear menu by clicking the gear button.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#gear-button']));

  // Wait for menu to not be hidden.
  await remoteCall.waitForElement(appId, '#gear-menu:not([hidden])');

  // Wait for menu item to appear.
  await remoteCall.waitForElement(
      appId, toggleMenuItemSelector + ':not([disabled])');

  // Wait for menu item to appear.
  await remoteCall.waitForElement(
      appId, toggleMenuItemSelector + ':not([checked])');

  // Click the menu item.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, [toggleMenuItemSelector]));

  // Wait for item to be checked.
  await remoteCall.waitForElement(appId, toggleMenuItemSelector + '[checked]');

  // Check the hidden files are displayed.
  await remoteCall.waitForFiles(
      appId, TestEntryInfo.getExpectedRows(hiddenEntrySet),
      {ignoreFileSize: true, ignoreLastModifiedTime: true});

  // Repeat steps to toggle again.
  await remoteCall.waitForElement(appId, '#gear-button:not([hidden])');
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#gear-button']));

  await remoteCall.waitForElement(appId, '#gear-menu:not([hidden])');
  await remoteCall.waitForElement(
      appId, toggleMenuItemSelector + ':not([disabled])');
  await remoteCall.waitForElement(appId, toggleMenuItemSelector + '[checked]');
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, [toggleMenuItemSelector]));

  await remoteCall.waitForElement(
      appId, toggleMenuItemSelector + ':not([checked])');
  await remoteCall.waitForFiles(
      appId, TestEntryInfo.getExpectedRows(basicSet),
      {ignoreFileSize: true, ignoreLastModifiedTime: true});
}

/**
 * Tests toggling the show-hidden-files menu option on Downloads.
 */
testcase.showHiddenFilesDownloads = async function() {
  const appId = await setupAndWaitUntilReady(
      RootPath.DOWNLOADS, BASIC_LOCAL_ENTRY_SET_WITH_HIDDEN, []);

  await runHiddenFilesTest(
      appId, BASIC_LOCAL_ENTRY_SET_WITHOUT_HIDDEN,
      BASIC_LOCAL_ENTRY_SET_WITH_HIDDEN);
};

/**
 * Tests toggling the show-hidden-files menu option on Drive.
 */
testcase.showHiddenFilesDrive = async function() {
  const appId = await setupAndWaitUntilReady(
      RootPath.DRIVE, [], BASIC_DRIVE_ENTRY_SET_WITH_HIDDEN);

  await runHiddenFilesTest(
      appId, BASIC_DRIVE_ENTRY_SET, BASIC_DRIVE_ENTRY_SET_WITH_HIDDEN);
};

/**
 * Tests that toggle-hidden-android-folders menu item exists when "Play files"
 * is selected, but hidden in Recents.
 */
testcase.showToggleHiddenAndroidFoldersGearMenuItemsInMyFiles =
    async function() {
  // Open Files.App on Play Files.
  const appId = await openNewWindow(RootPath.ANDROID_FILES);
  await addEntries(['android_files'], BASIC_ANDROID_ENTRY_SET);

  // Wait for the file list to appear.
  await remoteCall.waitForElement(appId, '#file-list');

  // Wait for the gear menu button to appear.
  await remoteCall.waitForElement(appId, '#gear-button:not([hidden])');

  // Click the gear menu button.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#gear-button']));

  // Wait for the gear menu to appear.
  await remoteCall.waitForElement(appId, '#gear-menu:not([hidden])');

  // #toggle-hidden-android-folders command should be shown and disabled by
  // default.
  await remoteCall.waitForElement(
      appId,
      '#gear-menu-toggle-hidden-android-folders' +
          ':not([checked]):not([hidden])');

  // Click the file list: the gear menu should hide.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#file-list']));

  // Wait for the gear menu to hide.
  await remoteCall.waitForElement(appId, '#gear-menu[hidden]');

  // Navigate to Recent.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['span[root-type-icon=\'recent\']']));

  // Click the gear menu button.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#gear-button']));

  // Wait for the gear menu to appear.
  await remoteCall.waitForElement(appId, '#gear-menu:not([hidden])');

  // #toggle-hidden-android-folders command should be hidden.
  await remoteCall.waitForElement(
      appId, '#gear-menu-toggle-hidden-android-folders[hidden]');
};

/**
 * Tests that "Play files" shows the full set of files after
 * toggle-hidden-android-folders is enabled.
 */
testcase.enableToggleHiddenAndroidFoldersShowsHiddenFiles = async function() {
  // Open Files.App on Play Files.
  const appId = await openNewWindow(RootPath.ANDROID_FILES);
  await addEntries(['android_files'], BASIC_ANDROID_ENTRY_SET_WITH_HIDDEN);

  // Wait for the file list to appear.
  await remoteCall.waitForElement(appId, '#file-list');

  // Wait for the gear menu button to appear.
  await remoteCall.waitForElement(appId, '#gear-button:not([hidden])');
  await runAndroidHiddenFilesTest(
      appId, BASIC_ANDROID_ENTRY_SET, BASIC_ANDROID_ENTRY_SET_WITH_HIDDEN);
};

/**
 * Tests that the current directory is changed to "Play files" after the
 * current directory is hidden by toggle-hidden-android-folders option.
 */
testcase.hideCurrentDirectoryByTogglingHiddenAndroidFolders = async function() {
  const MENU_ITEM_SELECTOR = '#gear-menu-toggle-hidden-android-folders';
  const appId = await openNewWindow(RootPath.ANDROID_FILES);
  await addEntries(['android_files'], BASIC_ANDROID_ENTRY_SET_WITH_HIDDEN);

  // Wait for the file list to appear.
  await remoteCall.waitForElement(appId, '#file-list');

  // Wait for the gear menu button to appear.
  await remoteCall.waitForElement(appId, '#gear-button:not([hidden])');

  // Open the gear menu by clicking the gear button.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#gear-button']));

  // Wait for menu to not be hidden.
  await remoteCall.waitForElement(appId, '#gear-menu:not([hidden])');

  // Wait for menu item to appear.
  await remoteCall.waitForElement(
      appId, MENU_ITEM_SELECTOR + ':not([disabled]):not([checked])');

  // Click the menu item.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, [MENU_ITEM_SELECTOR]));

  // Wait for item to be checked.
  await remoteCall.waitForElement(appId, MENU_ITEM_SELECTOR + '[checked]');

  // Check the hidden files are displayed.
  await remoteCall.waitForFiles(
      appId, TestEntryInfo.getExpectedRows(BASIC_ANDROID_ENTRY_SET_WITH_HIDDEN),
      {ignoreFileSize: true, ignoreLastModifiedTime: true});

  // Navigate to "/My files/Play files/A".
  await remoteCall.navigateWithDirectoryTree(
      appId, '/A', 'My files/Play files', 'android_files');

  // Wait until current directory is changed to "/My files/Play files/A".
  await remoteCall.waitUntilCurrentDirectoryIsChanged(
      appId, '/My files/Play files/A');

  // Open the gear menu by clicking the gear button.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#gear-button']));

  // Wait for menu to not be hidden.
  await remoteCall.waitForElement(appId, '#gear-menu:not([hidden])');

  // Wait for menu item to appear.
  await remoteCall.waitForElement(
      appId, MENU_ITEM_SELECTOR + '[checked]:not([disabled])');

  // Click the menu item.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, [MENU_ITEM_SELECTOR]));

  // Wait until the current directory is changed from
  // "/My files/Play files/A" to "/My files/Play files" since
  // "/My files/Play files/A" is invisible now.
  await remoteCall.waitUntilCurrentDirectoryIsChanged(
      appId, '/My files/Play files');
};

/**
 * Tests the paste-into-current-folder menu item.
 */
testcase.showPasteIntoCurrentFolder = async function() {
  const entrySet = [ENTRIES.hello, ENTRIES.world];

  // Add files to Downloads volume.
  await addEntries(['local'], entrySet);

  // Open Files.App on Downloads.
  const appId = await openNewWindow(RootPath.DOWNLOADS);
  await remoteCall.waitForElement(appId, '#file-list');

  // Wait for the files to appear in the file list.
  await remoteCall.waitForFiles(appId, TestEntryInfo.getExpectedRows(entrySet));

  // Wait for the gear menu button to appear.
  await remoteCall.waitForElement(appId, '#gear-button');

  // 1. Before selecting entries: click the gear menu button.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#gear-button']));

  // Wait for the gear menu to appear.
  await remoteCall.waitForElement(appId, '#gear-menu:not([hidden])');

  // #paste-into-current-folder command is shown. It should be disabled
  // because no file has been copied to clipboard.
  await remoteCall.waitForElement(
      appId,
      '#gear-menu cr-menu-item' +
          '[command=\'#paste-into-current-folder\']' +
          '[disabled]:not([hidden])');
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#file-list']));

  await remoteCall.waitForElement(appId, '#gear-menu[hidden]');

  // 2. Selecting a single regular file
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'selectFile', appId, [ENTRIES.hello.nameText]));

  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#gear-button']));

  // Wait for menu to appear.
  // The command is still shown.
  await remoteCall.waitForElement(
      appId,
      '#gear-menu:not([hidden]) cr-menu-item' +
          '[command=\'#paste-into-current-folder\']' +
          '[disabled]:not([hidden])');
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#file-list']));

  await remoteCall.waitForElement(appId, '#gear-menu[hidden]');

  // 3. When ready to paste a file
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'selectFile', appId, [ENTRIES.hello.nameText]));

  // Ctrl-C to copy the selected file
  await remoteCall.fakeKeyDown(appId, '#file-list', 'c', true, false, false);
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#gear-button']));

  // The command appears enabled.
  await remoteCall.waitForElement(
      appId,
      '#gear-menu:not([hidden])' +
          ' cr-menu-item[command=\'#paste-into-current-folder\']' +
          ':not([disabled]):not([hidden])');
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#file-list']));

  await remoteCall.waitForElement(appId, '#gear-menu[hidden]');
};

/**
 * Tests the "select-all" menu item.
 */
testcase.showSelectAllInCurrentFolder = async function() {
  const entrySet = [ENTRIES.newlyAdded];

  // Open Files.App on Downloads.
  const appId = await openNewWindow(RootPath.DOWNLOADS);
  await remoteCall.waitForElement(appId, '#file-list');

  // Wait for the gear menu button to appear.
  await remoteCall.waitForElement(appId, '#gear-button');

  // Click the gear menu button.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#gear-button']));

  // Wait for the gear menu to appear.
  await remoteCall.waitForElement(appId, '#gear-menu:not([hidden])');

  // Check: #select-all command is shown, but disabled (no files yet).
  await remoteCall.waitForElement(
      appId,
      '#gear-menu cr-menu-item' +
          '[command=\'#select-all\']' +
          '[disabled]:not([hidden])');

  // Click the file list: the gear menu should hide.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#file-list']));

  // Wait for the gear menu to hide.
  await remoteCall.waitForElement(appId, '#gear-menu[hidden]');

  // Add a new file to Downloads.
  await addEntries(['local'], entrySet);

  // Wait for the file list change.
  await remoteCall.waitForFiles(appId, TestEntryInfo.getExpectedRows(entrySet));

  // Click on the gear button again.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#gear-button']));

  // Check: #select-all command is shown, and enabled (there are files).
  await remoteCall.waitForElement(
      appId,
      '#gear-menu:not([hidden]) cr-menu-item' +
          '[command=\'#select-all\']' +
          ':not([disabled]):not([hidden])');

  // Click on the #gear-menu-select-all item.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#gear-menu-select-all']));

  // Check: the file-list should be selected.
  await remoteCall.waitForElement(appId, '#file-list li[selected]');
};

/**
 * Tests that new folder appears in the gear menu with Downloads focused in the
 * directory tree.
 */
testcase.newFolderInDownloads = async function() {
  const appId =
      await setupAndWaitUntilReady(RootPath.DOWNLOADS, [ENTRIES.hello], []);

  // Focus the directory tree.
  await remoteCall.callRemoteTestUtil('focus', appId, ['#directory-tree']);

  // Open the gear menu.
  await remoteCall.waitForElement(appId, '#gear-button');

  // Open the gear meny by a shortcut (Alt-E).
  chrome.test.assertTrue(
      await remoteCall.fakeKeyDown(appId, 'body', 'e', false, false, true));

  // Wait for menu to appear.
  await remoteCall.waitForElement(appId, '#gear-menu');

  // Wait for menu to appear, containing new folder.
  await remoteCall.waitForElement(
      appId, '#gear-menu-newfolder:not([disabled]):not([hidden])');
};

/**
 * Tests that Send feedback appears in the gear menu.
 */
testcase.showSendFeedbackAction = async function() {
  const entrySet = [ENTRIES.newlyAdded];

  // Open Files.App on Downloads.
  const appId = await openNewWindow(RootPath.DOWNLOADS);
  await remoteCall.waitForElement(appId, '#file-list');

  // Wait for the gear menu button to appear.
  await remoteCall.waitForElement(appId, '#gear-button');

  // Click the gear menu button.
  chrome.test.assertTrue(await remoteCall.callRemoteTestUtil(
      'fakeMouseClick', appId, ['#gear-button']));

  // Wait for the gear menu to appear.
  await remoteCall.waitForElement(appId, '#gear-menu:not([hidden])');

  // Check #send-feedback is shown and it's enabled.
  await remoteCall.waitForElement(
      appId,
      '#gear-menu:not([hidden]) cr-menu-item' +
          '[command=\'#send-feedback\']' +
          ':not([disabled]):not([hidden])');
};
