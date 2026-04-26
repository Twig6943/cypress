var profileGreetings = [
    'Welcome, {name}',
    'Welcome back, {name}',
    'Hello, {name}',
    'Good to see you, {name}',
    'Nice to see you again, {name}',
    '{name}, welcome',
    '{name}, you have returned',
    'Greetings, {name}',
    'It is good to see you, {name}',
    '{name} has logged in',
];

var currentGreetingTemplate = null;

function pickGreeting() {
    currentGreetingTemplate = profileGreetings[Math.floor(Math.random() * profileGreetings.length)];
}

function getProfileGreeting(name) {
    if (!currentGreetingTemplate) pickGreeting();
    return currentGreetingTemplate.replace('{name}', name || 'Stranger');
}

// full refresh, re-rolls greeting and updates avatar + text everywhere
function updateProfileWidget() {
    pickGreeting();
    syncProfileDisplay();
}

// just syncs the display text without re-rolling
function syncProfileDisplay() {
    var name = document.getElementById('username').value || '';
    var avatar = document.getElementById('profileAvatar');
    var greeting = document.getElementById('profileGreeting');
    var cardAvatar = document.getElementById('profileCardAvatar');
    var cardGreeting = document.getElementById('profileCardGreeting');
    var initial = name ? name.charAt(0).toUpperCase() : '?';
    var greetText = getProfileGreeting(name);
    if (avatar && !avatar.querySelector('img')) avatar.textContent = initial;
    if (greeting) greeting.textContent = greetText;
    if (cardAvatar && !cardAvatar.querySelector('img')) cardAvatar.textContent = initial;
    if (cardGreeting) cardGreeting.textContent = greetText;
}

function onProfileFieldChanged() {
    syncProfileDisplay();
    send('saveProfile', {
        username: document.getElementById('username').value,
        fov: document.getElementById('fov').value,
        additionalArgs: document.getElementById('additionalArgs').value
    });
}

function onDarkModeToggled(enabled) {
    applyDarkMode(enabled);
    send('saveProfile', {
        username: document.getElementById('username').value,
        fov: document.getElementById('fov').value,
        additionalArgs: document.getElementById('additionalArgs').value,
        darkMode: enabled
    });
}

function applyDarkMode(enabled) {
    document.body.classList.toggle('dark-mode', enabled);
    var toggle = document.getElementById('darkModeToggle');
    if (toggle) toggle.checked = enabled;
    try { localStorage.setItem('cypress_dark', enabled ? '1' : '0'); } catch(e) {}
}
