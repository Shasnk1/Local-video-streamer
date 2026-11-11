// Edit this file freely to change the website's behavior.

const grid = document.getElementById('grid');
const ipLink = document.getElementById('ipLink');
const movieCount = document.getElementById('movieCount');

async function load() {
    try {
        const res = await fetch('/api/movies');
        const data = await res.json();

        // Show the network IP prominently so other devices can connect.
        if (data.ip) {
            ipLink.href = 'http://' + data.ip + ':8080';
            ipLink.textContent = data.ip + ':8080';
        }

        render(data.movies);
    } catch (err) {
        grid.innerHTML = '<div class="empty">Could not load movies. Is the server running?</div>';
    }
}

function render(movies) {
    if (!movies || movies.length === 0) {
        grid.innerHTML = '<div class="empty">No movies found. Add video files to the movies folder.</div>';
        movieCount.textContent = '0 movies';
        return;
    }

    movieCount.textContent = movies.length + (movies.length === 1 ? ' movie' : ' movies');

    grid.innerHTML = movies.map(m => {
        const thumb = m.thumb
            ? '<img src="' + m.thumb + '" loading="lazy" alt="' + escapeHtml(m.title) + '">'
            : '<div class="thumb-placeholder">' + escapeHtml(m.title) + '</div>';
        return '<div class="card" onclick="play(\'' + escapeAttr(m.filename) + '\')">'
            + thumb
            + '<div class="info"><div class="title">' + escapeHtml(m.title) + '</div>'
            + (m.year ? '<div class="year">' + escapeHtml(m.year) + '</div>' : '')
            + '</div></div>';
    }).join('');
}

function play(filename) {
    window.location.href = '/movie/' + encodeURIComponent(filename);
}

function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, c => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
    }[c]));
}

function escapeAttr(s) {
    return String(s).replace(/\\/g, '\\\\').replace(/'/g, "\\'");
}

// Auto-refresh: the server updates whenever files are added/removed.
load();
setInterval(load, 10000);