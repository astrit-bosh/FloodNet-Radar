// Flood Sensor — Google Apps Script
// Receives POST from nRF9151 and writes one row per frame to the sheet.
// If the sheet is blank, headers are written automatically.
//
// Deploy as: Web App → Execute as Me → Anyone can access

// ---------------------------------------------------------------------------
// Column headers — edit here to add, remove, or rename columns.
// Order must match the appendRow() call below.
// ---------------------------------------------------------------------------
var HEADERS = [
  "Google Timestamp",
  "Datetime (UTC)",
  "Frame #",
  "Num Peaks",
  "Distance 1 (m)",
  "Strength 1 (dB)",
  "Distance 2 (m)",
  "Strength 2 (dB)",
  "Distance 3 (m)",
  "Strength 3 (dB)",
  "Battery (mV)"
];

// ---------------------------------------------------------------------------
// Write headers if sheet is empty
// ---------------------------------------------------------------------------
function ensureHeaders(sheet) {
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(HEADERS);

    // Bold and freeze header row
    var headerRange = sheet.getRange(1, 1, 1, HEADERS.length);
    headerRange.setFontWeight("bold");
    sheet.setFrozenRows(1);
  }
}

// ---------------------------------------------------------------------------
// POST handler
// ---------------------------------------------------------------------------
function doPost(e) {
  try {
    var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
    ensureHeaders(sheet);

    var data = JSON.parse(e.postData.contents);
    var googleTimestamp = new Date();
    var battery_mv = data.battery_mv !== undefined ? data.battery_mv : "";

    if (!data.frames || !Array.isArray(data.frames)) {
      return ContentService
        .createTextOutput(JSON.stringify({ status: "error", message: "No frames array" }))
        .setMimeType(ContentService.MimeType.JSON);
    }

    for (var i = 0; i < data.frames.length; i++) {
      var f = data.frames[i];

      // Convert Unix ms to ISO 8601 UTC string
      var datetime = f.datetime ? new Date(f.datetime).toISOString() : "";

      // Must match HEADERS order above
      sheet.appendRow([
        googleTimestamp,                                    // Google Timestamp
        datetime,                                           // Datetime (UTC)
        f.frame       || "",                                // Frame #
        f.num_peaks   !== undefined ? f.num_peaks : "",     // Num Peaks
        f.d1          || "",                                // Distance 1
        f.s1          !== undefined ? f.s1 : "",            // Strength 1
        f.d2          || "",                                // Distance 2
        f.s2          !== undefined ? f.s2 : "",            // Strength 2
        f.d3          || "",                                // Distance 3
        f.s3          !== undefined ? f.s3 : "",            // Strength 3
        battery_mv                                          // Battery (mV)
      ]);
    }

    return ContentService
      .createTextOutput(JSON.stringify({ status: "ok", rows: data.frames.length }))
      .setMimeType(ContentService.MimeType.JSON);

  } catch (err) {
    return ContentService
      .createTextOutput(JSON.stringify({ status: "error", message: err.toString() }))
      .setMimeType(ContentService.MimeType.JSON);
  }
}

// ---------------------------------------------------------------------------
// GET handler — confirms endpoint is live
// ---------------------------------------------------------------------------
function doGet(e) {
  return ContentService
    .createTextOutput(JSON.stringify({ status: "ok", message: "Flood sensor endpoint active" }))
    .setMimeType(ContentService.MimeType.JSON);
}