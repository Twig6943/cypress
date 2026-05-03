#nullable enable
using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Web;
using Newtonsoft.Json.Linq;

#if WINDOWS
using System.Drawing;
using System.Windows.Forms;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;
#endif

namespace CypressLauncher;

internal sealed record EaLoginWindowResult(string? Code, string? Error, bool Cancelled);

#if WINDOWS
internal sealed class EaLoginWindow : Form
{
	private readonly string m_authUrl;
	private readonly string m_redirectUri;
	private readonly WebView2 m_webView;
	private readonly TaskCompletionSource<EaLoginWindowResult> m_tcs;
	private readonly string m_logPath;
	private bool m_completed;

	private EaLoginWindow(string authUrl, string redirectUri, TaskCompletionSource<EaLoginWindowResult> tcs)
	{
		m_authUrl = authUrl;
		m_redirectUri = redirectUri;
		m_tcs = tcs;
		m_logPath = Path.Combine(Path.GetTempPath(), "cypress_ea_auth.log");

		Text = "EA Sign In";
		StartPosition = FormStartPosition.CenterScreen;
		MinimumSize = new Size(900, 700);
		Size = new Size(1080, 820);

		string iconPath = Path.Combine(AppContext.BaseDirectory, "assets", "cypressicons", "ico", "Burbank-CypressIcon.ico");
		if (File.Exists(iconPath))
			Icon = new Icon(iconPath);

		m_webView = new WebView2
		{
			Dock = DockStyle.Fill
		};
		Controls.Add(m_webView);

		Load += OnLoadAsync;
		FormClosed += OnFormClosed;
	}

	internal static Task<EaLoginWindowResult> ShowAsync(string authUrl, string redirectUri)
	{
		var tcs = new TaskCompletionSource<EaLoginWindowResult>(TaskCreationOptions.RunContinuationsAsynchronously);
		var thread = new Thread(() =>
		{
			Application.EnableVisualStyles();
			Application.SetCompatibleTextRenderingDefault(false);
			using var form = new EaLoginWindow(authUrl, redirectUri, tcs);
			Application.Run(form);
		});

		thread.IsBackground = true;
		thread.SetApartmentState(ApartmentState.STA);
		thread.Start();

		return tcs.Task;
	}

	private async void OnLoadAsync(object? sender, EventArgs e)
	{
		try
		{
			Log("window load");
			Log("auth url: " + m_authUrl);
			Log("redirect uri: " + m_redirectUri);
			string userDataDir = Path.Combine(
				Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
				"Cypress",
				"WebView2",
				"ea-auth");
			Directory.CreateDirectory(userDataDir);
			Log("user data dir: " + userDataDir);
			m_webView.CreationProperties = new CoreWebView2CreationProperties
			{
				UserDataFolder = userDataDir
			};
			await m_webView.EnsureCoreWebView2Async();
			m_webView.CoreWebView2.Settings.AreDefaultContextMenusEnabled = false;
			m_webView.CoreWebView2.Settings.AreDevToolsEnabled = false;
			m_webView.CoreWebView2.Settings.IsStatusBarEnabled = false;
			m_webView.CoreWebView2.Settings.IsZoomControlEnabled = true;
			await m_webView.CoreWebView2.CallDevToolsProtocolMethodAsync("Network.enable", "{}");
			var requestReceiver = m_webView.CoreWebView2.GetDevToolsProtocolEventReceiver("Network.requestWillBeSent");
			requestReceiver.DevToolsProtocolEventReceived += OnDevToolsRequestWillBeSent;
			m_webView.CoreWebView2.WebMessageReceived += OnWebMessageReceived;
			await m_webView.CoreWebView2.AddScriptToExecuteOnDocumentCreatedAsync(BuildQrcInterceptScript());
			m_webView.CoreWebView2.SourceChanged += OnSourceChanged;
			m_webView.CoreWebView2.NavigationCompleted += OnNavigationCompleted;
			m_webView.CoreWebView2.NavigationStarting += OnNavigationStarting;
			m_webView.CoreWebView2.LaunchingExternalUriScheme += OnLaunchingExternalUriScheme;
			m_webView.CoreWebView2.NewWindowRequested += OnNewWindowRequested;
			m_webView.CoreWebView2.WebResourceResponseReceived += OnWebResourceResponseReceived;
			Log("navigate start");
			m_webView.CoreWebView2.Navigate(m_authUrl);
		}
		catch (Exception ex)
		{
			Log("load failed: " + ex);
			Complete(new EaLoginWindowResult(null, "Failed to open embedded login: " + ex.Message, false));
		}
	}

	private void OnSourceChanged(object? sender, CoreWebView2SourceChangedEventArgs e)
	{
		try { Log("source changed: " + (m_webView.CoreWebView2?.Source ?? "(null)")); } catch { }
	}

	private void OnNavigationCompleted(object? sender, CoreWebView2NavigationCompletedEventArgs e)
	{
		try
		{
			Log("nav completed: success=" + e.IsSuccess + " status=" + e.WebErrorStatus + " source=" + (m_webView.CoreWebView2?.Source ?? "(null)"));
		}
		catch { }
	}

	private void OnNavigationStarting(object? sender, CoreWebView2NavigationStartingEventArgs e)
	{
		Log("nav starting: " + (e.Uri ?? "(null)"));
		if (string.IsNullOrEmpty(e.Uri) || !IsRedirectUriMatch(e.Uri))
			return;

		Log("nav starting matched redirect");
		e.Cancel = true;
		HandleRedirectUri(e.Uri);
	}

	private void OnLaunchingExternalUriScheme(object? sender, CoreWebView2LaunchingExternalUriSchemeEventArgs e)
	{
		Log("external uri: " + (e.Uri ?? "(null)"));
		if (string.IsNullOrEmpty(e.Uri) || !IsRedirectUriMatch(e.Uri))
			return;

		Log("external uri matched redirect");
		e.Cancel = true;
		HandleRedirectUri(e.Uri);
	}

	private void OnNewWindowRequested(object? sender, CoreWebView2NewWindowRequestedEventArgs e)
	{
		Log("new window: " + (e.Uri ?? "(null)"));
		if (!string.IsNullOrEmpty(e.Uri) && IsRedirectUriMatch(e.Uri))
		{
			Log("new window matched redirect");
			e.Handled = true;
			HandleRedirectUri(e.Uri);
			return;
		}

		e.Handled = true;
		if (!string.IsNullOrEmpty(e.Uri))
			m_webView.CoreWebView2.Navigate(e.Uri);
	}

	private void OnWebMessageReceived(object? sender, CoreWebView2WebMessageReceivedEventArgs e)
	{
		if (m_completed) return;

		try
		{
			var uri = e.TryGetWebMessageAsString();
			Log("web message: " + (uri ?? "(null)"));
			if (!string.IsNullOrEmpty(uri) && IsRedirectUriMatch(uri))
				HandleRedirectUri(uri);
		}
		catch { }
	}

	private async void OnWebResourceResponseReceived(object? sender, CoreWebView2WebResourceResponseReceivedEventArgs e)
	{
		if (m_completed) return;

		try
		{
			Log("response: " + (e.Request?.Uri ?? "(null)"));
			var response = e.Response;
			if (response == null) return;
			if (response.StatusCode < 300 || response.StatusCode >= 400) return;

			string location = "";
			try { location = response.Headers.GetHeader("Location") ?? ""; } catch { }
			Log("response redirect location: " + (string.IsNullOrEmpty(location) ? "(none)" : location));
			if (string.IsNullOrEmpty(location) || !IsRedirectUriMatch(location))
				return;

			HandleRedirectUri(location);
		}
		catch { }
	}

	private void OnDevToolsRequestWillBeSent(object? sender, CoreWebView2DevToolsProtocolEventReceivedEventArgs e)
	{
		if (m_completed) return;

		try
		{
			var payload = JObject.Parse(e.ParameterObjectAsJson);
			var requestUrl = (string?)payload["request"]?["url"] ?? "";
			if (!string.IsNullOrEmpty(requestUrl))
				Log("cdp request: " + requestUrl);
			if (!string.IsNullOrEmpty(requestUrl) && IsRedirectUriMatch(requestUrl))
			{
				Log("cdp request matched redirect");
				HandleRedirectUri(requestUrl);
				return;
			}

			var locationToken = payload["redirectResponse"]?["headers"]?["Location"]
				?? payload["redirectResponse"]?["headers"]?["location"];
			var location = (string?)locationToken ?? "";
			if (!string.IsNullOrEmpty(location))
				Log("cdp redirect location: " + location);
			if (!string.IsNullOrEmpty(location) && IsRedirectUriMatch(location))
				HandleRedirectUri(location);
		}
		catch { }
	}

	private void HandleRedirectUri(string uri)
	{
		if (m_completed) return;
		uri = NormalizeRedirectUri(uri);
		Log("handle redirect: " + uri);

		var queryIndex = uri.IndexOf('?');
		var query = queryIndex >= 0 ? HttpUtility.ParseQueryString(uri.Substring(queryIndex)) : HttpUtility.ParseQueryString("");
		string? code = query["code"];
		string? error = query["error"];

		if (!string.IsNullOrEmpty(error))
		{
			Log("redirect had error: " + error);
			Complete(new EaLoginWindowResult(null, error, false));
			return;
		}

		if (string.IsNullOrEmpty(code))
		{
			Log("redirect missing code");
			Complete(new EaLoginWindowResult(null, "No authorization code received", false));
			return;
		}

		Log("redirect got code");
		Complete(new EaLoginWindowResult(code, null, false));
	}

	private bool IsRedirectUriMatch(string uri)
	{
		var normalized = NormalizeRedirectUri(uri);
		return normalized.StartsWith(m_redirectUri, StringComparison.OrdinalIgnoreCase);
	}

	private string NormalizeRedirectUri(string uri)
	{
		if (string.IsNullOrEmpty(uri)) return uri;
		if (uri.StartsWith("qrc:/", StringComparison.OrdinalIgnoreCase) && !uri.StartsWith("qrc:///", StringComparison.OrdinalIgnoreCase))
			return "qrc:///" + uri.Substring("qrc:/".Length);
		return uri;
	}

	private string BuildQrcInterceptScript()
	{
		var redirect = m_redirectUri.Replace("\\", "\\\\").Replace("'", "\\'");
		return @"
(() => {
  const redirectPrefix = '__REDIRECT__';
  const send = (value) => {
    try {
      if (typeof value === 'string' && value.startsWith(redirectPrefix)) {
        chrome.webview.postMessage(value);
        return true;
      }
    } catch (e) {}
    return false;
  };

  const normalize = (value) => {
    try {
      return String(value || '');
    } catch (e) {
      return '';
    }
  };

  const wrapNav = (fn) => function(value) {
    const url = normalize(value);
    if (send(url)) return;
    return fn.apply(this, arguments);
  };

  try {
    const open = window.open;
    window.open = function(url) {
      const value = normalize(url);
      if (send(value)) return null;
      return open.apply(this, arguments);
    };
  } catch (e) {}

  try { window.location.assign = wrapNav(window.location.assign.bind(window.location)); } catch (e) {}
  try { window.location.replace = wrapNav(window.location.replace.bind(window.location)); } catch (e) {}

  document.addEventListener('click', (event) => {
    const anchor = event.target && event.target.closest ? event.target.closest('a[href]') : null;
    if (!anchor) return;
    const href = normalize(anchor.href);
    if (send(href)) event.preventDefault();
  }, true);

  document.addEventListener('submit', (event) => {
    const form = event.target;
    if (!form || !form.action) return;
    const action = normalize(form.action);
    if (send(action)) event.preventDefault();
  }, true);
})();
".Replace("__REDIRECT__", redirect);
	}

	private void OnFormClosed(object? sender, FormClosedEventArgs e)
	{
		Log("window closed");
		if (!m_completed)
			m_tcs.TrySetResult(new EaLoginWindowResult(null, null, true));
	}

	private void Complete(EaLoginWindowResult result)
	{
		if (m_completed) return;
		m_completed = true;
		Log("complete: cancelled=" + result.Cancelled + " error=" + (result.Error ?? "(none)") + " code=" + (string.IsNullOrEmpty(result.Code) ? "(none)" : "present"));
		m_tcs.TrySetResult(result);
		if (IsHandleCreated)
			BeginInvoke(new Action(Close));
		else
			Close();
	}

	private void Log(string message)
	{
		try
		{
			File.AppendAllText(m_logPath, $"[{DateTime.Now:O}] {message}{Environment.NewLine}");
		}
		catch { }
	}
}
#endif
