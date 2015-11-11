var sock = undefined;
var poll_id = undefined, retry_id = undefined;
var sock_onopen = function() {
	document.getElementById("status").textContent = "connected";
	poll_id = window.setInterval(function() {
		sock.send("!");
	}, 100);
};
var sock_onclose = function(e) {
	document.getElementById("status").textContent = "disconnected";
	if (typeof poll_id === "number") {
		window.clearInterval(poll_id);
		poll_id = undefined;
	}
	if (!e.wasClean) {
		retry_id = window.setTimeout(function() {
			sock = sock_connect();
		}, 1000);
	}
};
var sock_onmessage = function(e) {
	var msg = JSON.parse(e.data);
	document.getElementById("hostname").textContent = msg.hostname;
	document.getElementById("username").textContent = msg.username;
	document.getElementById("pid").textContent = msg.pid;
	document.getElementById("clients").textContent = msg.clients;
};
var sock_connect = function() {
	new_sock = new WebSocket("ws://" + window.location.host + "/ws");
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
