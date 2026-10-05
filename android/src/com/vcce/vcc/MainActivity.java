// VCCA, the Android shell for VCC: a full-screen WebView running the VCC core
// (compiled to WebAssembly) and its front end from the app's assets.
package com.vcce.vcc;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Base64;
import android.view.View;
import android.webkit.MimeTypeMap;
import android.view.Window;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.HashMap;
import java.util.Map;

public class MainActivity extends Activity {
    // Assets are served from a fake https origin so the page gets a secure
    // context (AudioWorklet, fetch of the .wasm) without touching file://.
    private static final String ORIGIN = "https://appassets.androidplatform.net/";
    private static final int REQ_OPEN = 1;
    private static final int REQ_SAVE = 2;

    private WebView web;
    private ValueCallback<Uri[]> pendingPick;
    private byte[] pendingSave;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setStatusBarColor(0xFF1C1E22);
        getWindow().setNavigationBarColor(0xFF1C1E22);

        web = new WebView(this);
        setContentView(web);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setDatabaseEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(true);
        s.setBuiltInZoomControls(false);
        s.setSupportZoom(false);
        web.setOverScrollMode(View.OVER_SCROLL_NEVER);
        web.setBackgroundColor(0xFF111214);
        web.addJavascriptInterface(new Host(), "AndroidHost");

        web.setWebViewClient(new WebViewClient() {
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest req) {
                String url = req.getUrl().toString();
                if (!url.startsWith(ORIGIN)) return null;
                String path = req.getUrl().getPath();
                if (path == null || path.equals("/")) path = "/index.html";
                try {
                    InputStream in = getAssets().open("web" + path);
                    WebResourceResponse r = new WebResourceResponse(mimeFor(path), null, in);
                    Map<String, String> h = new HashMap<>();
                    h.put("Cache-Control", "no-cache");
                    r.setResponseHeaders(h);
                    return r;
                } catch (Exception e) {
                    return new WebResourceResponse("text/plain", "utf-8", 404, "Not Found", null, null);
                }
            }

            @Override
            public boolean shouldOverrideUrlLoading(WebView view, String url) {
                if (url.startsWith(ORIGIN)) return false;
                // Links out of the app (the About credits) open in the browser.
                if (url.startsWith("https://") || url.startsWith("http://")) {
                    try { startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url))); } catch (Exception e) { /* no browser */ }
                }
                return true;
            }
        });

        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public boolean onShowFileChooser(WebView view, ValueCallback<Uri[]> callback, FileChooserParams params) {
                if (pendingPick != null) pendingPick.onReceiveValue(null);
                pendingPick = callback;
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");   // ROMs and disk images have no registered MIME type
                if (params.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE)
                    i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                try {
                    startActivityForResult(i, REQ_OPEN);
                } catch (Exception e) {
                    pendingPick = null;
                    return false;
                }
                return true;
            }
        });

        web.loadUrl(ORIGIN + "index.html");
    }

    private static String mimeFor(String path) {
        if (path.endsWith(".html")) return "text/html";
        if (path.endsWith(".js")) return "text/javascript";
        if (path.endsWith(".wasm")) return "application/wasm";
        if (path.endsWith(".css")) return "text/css";
        if (path.endsWith(".png")) return "image/png";
        return "application/octet-stream";
    }

    // A file's real type, so a video or picture is saved as one (and a
    // document provider does not tack ".bin" onto the name). Disk and tape
    // images have no registered type and stay application/octet-stream.
    private static String mimeForSave(String name) {
        int dot = name.lastIndexOf('.');
        String type = dot < 0 ? null : MimeTypeMap.getSingleton().getMimeTypeFromExtension(name.substring(dot + 1).toLowerCase());
        return type != null ? type : "application/octet-stream";
    }

    // Called from JavaScript: saves a disk image wherever the user chooses.
    // Large files (hard drive images) arrive in pieces: saveBegin, saveChunk..., saveEnd.
    private ByteArrayOutputStream pendingParts;
    private String pendingName;

    private class Host {
        // Settings live in the app's SharedPreferences as well as the WebView,
        // so they survive anything that clears web storage alone.
        @JavascriptInterface
        public String getPrefs() {
            return getSharedPreferences("vcca", MODE_PRIVATE).getString("settings", null);
        }

        @JavascriptInterface
        public void setPrefs(String json) {
            getSharedPreferences("vcca", MODE_PRIVATE).edit().putString("settings", json).apply();
        }

        @JavascriptInterface
        public void saveBegin(String name) {
            pendingName = name;
            pendingParts = new ByteArrayOutputStream();
        }

        @JavascriptInterface
        public void saveChunk(String base64) {
            byte[] b = Base64.decode(base64, Base64.DEFAULT);
            pendingParts.write(b, 0, b.length);
        }

        @JavascriptInterface
        public void saveEnd() {
            final byte[] all = pendingParts.toByteArray();
            pendingParts = null;
            runOnUiThread(new Runnable() {
                public void run() {
                    pendingSave = all;
                    Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                    i.addCategory(Intent.CATEGORY_OPENABLE);
                    i.setType(mimeForSave(pendingName));
                    i.putExtra(Intent.EXTRA_TITLE, pendingName);
                    startActivityForResult(i, REQ_SAVE);
                }
            });
        }

        @JavascriptInterface
        public void saveFile(final String name, final String base64) {
            runOnUiThread(new Runnable() {
                public void run() {
                    pendingSave = Base64.decode(base64, Base64.DEFAULT);
                    Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                    i.addCategory(Intent.CATEGORY_OPENABLE);
                    i.setType(mimeForSave(name));
                    i.putExtra(Intent.EXTRA_TITLE, name);
                    startActivityForResult(i, REQ_SAVE);
                }
            });
        }
    }

    @Override
    protected void onActivityResult(int req, int result, Intent data) {
        if (req == REQ_OPEN) {
            Uri[] uris = null;
            if (result == RESULT_OK && data != null) {
                if (data.getClipData() != null) {
                    uris = new Uri[data.getClipData().getItemCount()];
                    for (int k = 0; k < uris.length; k++) uris[k] = data.getClipData().getItemAt(k).getUri();
                } else if (data.getData() != null) {
                    uris = new Uri[]{data.getData()};
                }
            }
            if (pendingPick != null) pendingPick.onReceiveValue(uris);
            pendingPick = null;
        } else if (req == REQ_SAVE) {
            if (result == RESULT_OK && data != null && data.getData() != null && pendingSave != null) {
                try (OutputStream out = getContentResolver().openOutputStream(data.getData(), "wt")) {
                    out.write(pendingSave);
                    Toast.makeText(this, "Saved", Toast.LENGTH_SHORT).show();
                } catch (Exception e) {
                    Toast.makeText(this, "Save failed: " + e.getMessage(), Toast.LENGTH_LONG).show();
                }
            }
            pendingSave = null;
        }
    }

    @Override
    public void onBackPressed() {
        // Let the page close its menu or dialog first.
        web.evaluateJavascript("window.vccBack ? window.vccBack() : false", new ValueCallback<String>() {
            public void onReceiveValue(String handled) {
                if (!"true".equals(handled)) MainActivity.super.onBackPressed();
            }
        });
    }

    @Override
    protected void onPause() {
        super.onPause();
        web.evaluateJavascript("window.vccPause && window.vccPause(true)", null);
        web.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        web.onResume();
        web.evaluateJavascript("window.vccPause && window.vccPause(false)", null);
    }

}
