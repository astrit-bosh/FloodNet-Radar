// Flood Sensor — Google Apps Script
// Receives POST from nRF9151 and writes one row per frame to the raw data sheet.
// Also writes one median row per buffer to the processed data sheet.
//
// Deploy as: Web App → Execute as Me → Anyone can access

// ---------------------------------------------------------------------------
// Sheet names
// ---------------------------------------------------------------------------
var RAW_SHEET_NAME = "Raw Data";
var PROCESSED_SHEET_NAME = "Processed Data";

// Minimum empty rows before auto-expanding the sheet
var MIN_ROWS_BUFFER = 100;

// ---------------------------------------------------------------------------
// Column headers
// ---------------------------------------------------------------------------
var RAW_HEADERS = [
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
  "Battery (mV)",
];

var PROCESSED_HEADERS = [
  "Google Timestamp",
  "Datetime (UTC)",
  "Distance 1 (m)",
  "Strength 1 (dB)",
  "Distance 2 (m)",
  "Strength 2 (dB)",
  "Distance 3 (m)",
  "Strength 3 (dB)",
  "Battery (mV)",
  "Valid Frames",
];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
function getOrCreateSheet(ss, name) {
  var sheet = ss.getSheetByName(name);
  if (!sheet) {
    sheet = ss.insertSheet(name);
  }
  return sheet;
}

function ensureHeaders(sheet, headers) {
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(headers);
    var headerRange = sheet.getRange(1, 1, 1, headers.length);
    headerRange.setFontWeight("bold");
    sheet.setFrozenRows(1);
  }
}

function ensureRowsAvailable(sheet) {
  var maxRows = sheet.getMaxRows();
  var lastRow = sheet.getLastRow();
  if (maxRows - lastRow < MIN_ROWS_BUFFER) {
    sheet.insertRowsAfter(maxRows, 1000);
  }
}

function median(values) {
  if (values.length === 0) return "";
  values.sort(function (a, b) {
    return a - b;
  });
  var mid = Math.floor(values.length / 2);
  if (values.length % 2 === 0) {
    return (values[mid - 1] + values[mid]) / 2;
  } else {
    return values[mid];
  }
}

// ---------------------------------------------------------------------------
// POST handler
// ---------------------------------------------------------------------------
function doPost(e) {
  try {
    var ss = SpreadsheetApp.getActiveSpreadsheet();

    // Get or create both sheets
    var rawSheet = getOrCreateSheet(ss, RAW_SHEET_NAME);
    var processedSheet = getOrCreateSheet(ss, PROCESSED_SHEET_NAME);

    ensureHeaders(rawSheet, RAW_HEADERS);
    ensureHeaders(processedSheet, PROCESSED_HEADERS);
    ensureRowsAvailable(rawSheet);
    ensureRowsAvailable(processedSheet);

    var data = JSON.parse(e.postData.contents);
    var googleTimestamp = new Date();
    var battery_mv = data.battery_mv !== undefined ? data.battery_mv : "";

    if (!data.frames || !Array.isArray(data.frames)) {
      return ContentService.createTextOutput(
        JSON.stringify({ status: "error", message: "No frames array" }),
      ).setMimeType(ContentService.MimeType.JSON);
    }

    // Accumulators for median calculation — one array per peak per field
    var d1 = [],
      s1 = [],
      d2 = [],
      s2 = [],
      d3 = [],
      s3 = [];
    var validFrames = 0;
    var firstDatetime = "";

    for (var i = 0; i < data.frames.length; i++) {
      var f = data.frames[i];
      var datetime =
        f.datetime && f.datetime > 0 ? new Date(f.datetime).toISOString() : "";
      if (i === 0) firstDatetime = datetime;

      // Write raw row
      rawSheet.appendRow([
        googleTimestamp,
        datetime,
        f.frame || "",
        f.num_peaks !== undefined ? f.num_peaks : "",
        f.d1 || "",
        f.s1 !== undefined ? f.s1 : "",
        f.d2 || "",
        f.s2 !== undefined ? f.s2 : "",
        f.d3 || "",
        f.s3 !== undefined ? f.s3 : "",
        battery_mv,
      ]);

      // Accumulate valid values for median (exclude frames with 0 peaks)
      if (f.num_peaks > 0) {
        validFrames++;

        // Peak 1 — present in all valid frames
        if (f.d1 !== undefined && f.d1 !== "" && f.d1 !== 0) d1.push(f.d1);
        if (f.s1 !== undefined && f.s1 !== "") s1.push(f.s1);

        // Peak 2 — only present if num_peaks >= 2
        if (f.num_peaks >= 2) {
          if (f.d2 !== undefined && f.d2 !== "" && f.d2 !== 0) d2.push(f.d2);
          if (f.s2 !== undefined && f.s2 !== "") s2.push(f.s2);
        }

        // Peak 3 — only present if num_peaks >= 3
        if (f.num_peaks >= 3) {
          if (f.d3 !== undefined && f.d3 !== "" && f.d3 !== 0) d3.push(f.d3);
          if (f.s3 !== undefined && f.s3 !== "") s3.push(f.s3);
        }
      }
    }

    // Write one processed row with medians
    processedSheet.appendRow([
      googleTimestamp, // Google Timestamp
      firstDatetime, // Datetime (UTC) — from first frame in buffer
      median(d1), // Distance 1 (m)
      median(s1), // Strength 1 (dB)
      median(d2), // Distance 2 (m)
      median(s2), // Strength 2 (dB)
      median(d3), // Distance 3 (m)
      median(s3), // Strength 3 (dB)
      battery_mv, // Battery (mV)
      validFrames, // Valid Frames
    ]);

    return ContentService.createTextOutput(
      JSON.stringify({ status: "ok", rows: data.frames.length }),
    ).setMimeType(ContentService.MimeType.JSON);
  } catch (err) {
    return ContentService.createTextOutput(
      JSON.stringify({ status: "error", message: err.toString() }),
    ).setMimeType(ContentService.MimeType.JSON);
  }
}

// ---------------------------------------------------------------------------
// GET handler — confirms endpoint is live
// ---------------------------------------------------------------------------
function doGet(e) {
  return ContentService.createTextOutput(
    JSON.stringify({ status: "ok", message: "Flood sensor endpoint active" }),
  ).setMimeType(ContentService.MimeType.JSON);
}
