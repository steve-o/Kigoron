// Whole-script strict mode syntax
"use strict";
class KigoronPoller {
	constructor(url) {
		this.url = url;
		this.sock = undefined;
		this.poll_id = undefined;
		this.retry_id = undefined;
	}

	Connect() {
		let new_sock = new WebSocket(this.url);
		new_sock.onopen = (e) => this.OnOpen(e);
		new_sock.onclose = (e) => this.OnClose(e);
		new_sock.onmessage = (e) => this.OnMessage(e);
		this.sock = new_sock;
	}

	OnOpen() {
		document.getElementById("status").textContent = "connected";
		this.SchedulePoll();
	}

	Close() {
		if (typeof this.retry_id === "number") {
			window.clearTimeout(this.retry_id);
			this.retry_id = undefined;
		}
		this.CancelPoll();
		if (this.sock !== undefined) {
			this.sock.close();
			this.sock = undefined;
		}
	}

	OnClose(e) {
		this.CancelPoll();
		if (!e.wasClean) {
			document.getElementById("status").textContent = "disconnected";
			let timeout = 1000;
			if (!document.hasFocus()) {
				timeout *= 2;
			}
			this.retry_id = window.setTimeout(() => this.Connect(), timeout);
		}
	}

	SchedulePoll() {
		let timeout = 100;
		if (!document.hasFocus()) {
			timeout *= 2;
		}
		this.poll_id = window.setTimeout(() => this.SendPoll(), timeout);
	}

	CancelPoll() {
		if (typeof this.poll_id === "number") {
			window.clearTimeout(this.poll_id);
			this.poll_id = undefined;
		}
	}

	SendPoll() {
		if (this.sock.readyState !== WebSocket.OPEN)
			return;
		if (this.sock.bufferedAmount === 0)
			this.sock.send("!");
	}

	OnMessage(e) {
		let msg = JSON.parse(e.data);
		window.requestAnimationFrame(() => this.OnUpdate(msg));
		this.SchedulePoll();
	}

	OnUpdate(msg) {
		document.getElementById("hostname").textContent = msg.hostname;
		document.getElementById("username").textContent = msg.username;
		document.getElementById("pid").textContent = msg.pid;
		document.getElementById("clients").textContent = msg.clients;
	}

	OnHidden() {
		document.getElementById("status").textContent = "paused";
		this.CancelPoll();
	}

	OnVisible() {
		if (this.poll_id === undefined &&
			this.sock !== undefined &&
			this.sock.readyState === WebSocket.OPEN)
		{
			this.OnOpen();
		}
	}
}

let poller = new KigoronPoller("ws://" + window.location.host + "/ws");
poller.Connect();

document.addEventListener("visibilitychange", function() {
	switch(document.visibilityState) {
	case "hidden":
		poller.OnHidden();
		break;
	case "unloaded":
		poller.Close();
		break;
	case "visible":
		poller.OnVisible();
		break;
	}
});
