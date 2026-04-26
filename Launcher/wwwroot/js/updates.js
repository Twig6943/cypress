// auto-update checks for launcher + server repos
var pendingUpdates = {};

function onUpdateCheckResult(data) {
    var container = document.getElementById('updateBannerContainer');
    if (!container) return;
    container.innerHTML = '';
    pendingUpdates = {};

    if (!data.updates || data.updates.length === 0) return;

    data.updates.forEach(function (u) {
        pendingUpdates[u.channel] = u;

        var banner = document.createElement('div');
        banner.className = 'update-banner';
        banner.id = 'updateBanner_' + u.channel;
        banner.innerHTML =
            '<div class="update-banner-text">' +
                '<strong>' + escapeHtml(u.name) + '</strong> ' +
                '<span class="update-versions">' + escapeHtml(u.currentVersion) + ' &rarr; ' + escapeHtml(u.latestVersion) + '</span>' +
            '</div>' +
            '<div class="update-banner-actions">' +
                '<button class="btn btn-sm btn-primary update-btn" onclick="startUpdate(\'' + u.channel + '\')">Update</button>' +
                '<button class="btn btn-sm btn-secondary update-dismiss-btn" onclick="dismissUpdate(\'' + u.channel + '\')">Later</button>' +
            '</div>' +
            '<div class="update-progress" id="updateProgress_' + u.channel + '" style="display:none;">' +
                '<div class="update-progress-bar" id="updateProgressBar_' + u.channel + '"></div>' +
            '</div>';
        container.appendChild(banner);
    });
}

function startUpdate(channel) {
    var u = pendingUpdates[channel];
    if (!u) return;

    var btn = document.querySelector('#updateBanner_' + channel + ' .update-btn');
    if (btn) { btn.disabled = true; btn.textContent = 'Downloading...'; }

    var dismiss = document.querySelector('#updateBanner_' + channel + ' .update-dismiss-btn');
    if (dismiss) dismiss.style.display = 'none';

    var progress = document.getElementById('updateProgress_' + channel);
    if (progress) progress.style.display = '';

    send('startUpdate', { channel: channel, assetUrl: u.assetUrl, latestVersion: u.latestVersion });
}

function onUpdateProgress(data) {
    var bar = document.getElementById('updateProgressBar_' + data.channel);
    if (bar) bar.style.width = data.percent + '%';
}

function onUpdateComplete(data) {
    var banner = document.getElementById('updateBanner_' + data.channel);
    if (!banner) return;

    if (data.channel === 'launcher') {
        banner.innerHTML = '<div class="update-banner-text">Restarting...</div>';
    } else {
        banner.innerHTML = '<div class="update-banner-text"><strong>' + escapeHtml(pendingUpdates[data.channel]?.name || data.channel) + '</strong> updated to ' + escapeHtml(data.version) + '</div>';
        setTimeout(function () { banner.remove(); }, 5000);
    }
    delete pendingUpdates[data.channel];
}

function onUpdateError(data) {
    var banner = document.getElementById('updateBanner_' + data.channel);
    if (banner) {
        banner.innerHTML =
            '<div class="update-banner-text update-error">Update failed: ' + escapeHtml(data.error) + '</div>' +
            '<button class="btn btn-sm btn-secondary" onclick="dismissUpdate(\'' + data.channel + '\')">Dismiss</button>';
    }
}

function dismissUpdate(channel) {
    var banner = document.getElementById('updateBanner_' + channel);
    if (banner) banner.remove();
    delete pendingUpdates[channel];
}
