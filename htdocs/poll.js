// Whole-script strict mode syntax
"use strict";

let sock = undefined;
let poll_id = undefined, retry_id = undefined;
let sock_onopen = function() {
	document.getElementById("status").textContent = "connected";
	sock_poll();
};
let sock_onclose = function(e) {
	document.getElementById("status").textContent = "disconnected";
	if (typeof poll_id === "number") {
		window.clearTimeout(poll_id);
		poll_id = undefined;
	}
	if (!e.wasClean) {
		let timeout = 1000;
		if (!document.hasFocus()) {
			timeout *= 2;
		}
		retry_id = window.setTimeout(function() {
			sock = sock_connect();
		}, timeout);
	}
};
let sock_poll = function() {
	let timeout = 100;
	if (!document.hasFocus()) {
		timeout *= 2;
	}
	poll_id = window.setTimeout(function() {
		if (sock.readyState == WebSocket.OPEN) {
			sock.send("!");
		}
	}, timeout);
}
let sock_onerror = function(e) {
	console.error(e);
}
let sock_onmessage = function(e) {
	let msg = JSON.parse(e.data);
	document.getElementById("hostname").textContent = msg.hostname;
	document.getElementById("username").textContent = msg.username;
	document.getElementById("pid").textContent = msg.pid;
	document.getElementById("clients").textContent = msg.clients;
	sock_poll();
};
let sock_connect = function() {
	let new_sock = new WebSocket("ws://" + window.location.host + "/ws");
	new_sock.onopen = sock_onopen;
	new_sock.onclose = sock_onclose;
	new_sock.onmessage = sock_onmessage;
	return new_sock;
};
(function() {
	sock = sock_connect();
	document.addEventListener("visibilitychange", function() {
		switch (document.visibilityState) {
		case "hidden":
		case "unloaded":
			document.getElementById("status").textContent = "paused";
			sock.onclose({ wasClean: true });
			break;
		case "visible":
			if (poll_id === undefined && sock.readyState == WebSocket.OPEN) {
				sock.onopen();
			}
			break;
		}
	});
})();
