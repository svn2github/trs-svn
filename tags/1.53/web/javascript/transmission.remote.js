/*
 * Copyright © Dave Perrett and Malcolm Jarvis
 * This code is licensed under the GPL version 2.
 * For details, see http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * Class TransmissionRemote
 */

function RPC() { }
//Prefs.prototype = { }

// Constants
RPC._Root                   = '/transmission/rpc';
RPC._Encryption             = 'encryption';
RPC._EncryptionPreferred    = 'preferred';
RPC._EncryptionRequired     = 'required';
RPC._UpSpeedLimit           = 'speed-limit-up';
RPC._DownSpeedLimit         = 'speed-limit-down';
RPC._DownloadDir            = 'download-dir';
RPC._PeerPort               = 'port';
RPC._UpSpeedLimited         = 'speed-limit-up-enabled';
RPC._DownSpeedLimited       = 'speed-limit-down-enabled';

function TransmissionRemote( controller )
{
	this.initialize( controller );
	return this;
}

TransmissionRemote.prototype =
{
	/*
	 * Constructor
	 */
	initialize: function(controller) {
		this._controller = controller;
		this._error = '';
		this._token = '';
	},

	/*
	 * Display an error if an ajax request fails, and stop sending requests
	 * or, on a 409, globally set the X-Transmission-Session-Id and resend
	 */
	ajaxError: function(request, error_string, exception, ajaxObject) {
		remote = this;


		// set the Transmission-Session-Id on a 409 
		if(request.status == 409 && (token = request.getResponseHeader('X-Transmission-Session-Id'))){ 
			remote._token = token; 
			$.ajax(ajaxObject); 
			return; 
		} 

		remote._error = request.responseText
			? request.responseText.trim().replace(/(<([^>]+)>)/ig,"")
			: "";
		if( !remote._error.length )
			remote._error = 'Server not responding';
		
		dialog.confirm('Connection Failed', 
			'Could not connect to the server. You may need to reload the page to reconnect.', 
			'Details',
			'alert(transmission.remote._error);',
			null,
			'Dismiss');
		transmission.togglePeriodicRefresh(false);
	},


	appendSessionId: function(XHR) { 
		XHR.setRequestHeader('X-Transmission-Session-Id', this._token); 
	}, 

	sendRequest: function( data, success )
	{
		remote = this;
		$.ajax( {
			url: RPC._Root,
			type: 'POST',
			contentType: 'json',
			dataType: 'json',
			cache: false,
			data: $.toJSON(data),
			beforeSend: function(XHR){ remote.appendSessionId(XHR) },
			error: function(request, error_string, exception){ remote.ajaxError(request, error_string, exception, this) },
			success: success
		} );
	},

	loadDaemonPrefs: function() {
		var tr = this._controller;
		var o = { method: 'session-get' };
		this.sendRequest( o, function(data) {
			var o = data.arguments;
			Prefs.getClutchPrefs( o );
			tr.updatePrefs( o );
		} );
	},

	loadTorrents: function() {
		var tr = this._controller;
		var o = {
			method: 'torrent-get',
			arguments: { fields: [
				'addedDate', 'announceURL', 'comment', 'creator',
				'dateCreated', 'downloadedEver', 'error', 'errorString',
				'eta', 'hashString', 'haveUnchecked', 'haveValid', 'id',
				'isPrivate', 'leechers', 'leftUntilDone', 'name',
				'peersConnected', 'peersGettingFromUs', 'peersSendingToUs',
				'rateDownload', 'rateUpload', 'seeders', 'sizeWhenDone',
				'status', 'swarmSpeed', 'totalSize', 'uploadedEver' ]
			}
		};
		this.sendRequest( o, function(data) {
			tr.updateTorrents( data.arguments.torrents );
		}, "json" );
	},

	sendTorrentCommand: function( method, torrents ) {
		var remote = this;
		var o = {
			method: method,
			arguments: { ids: [ ] }
		};
		if( torrents != null )
			for( var i=0, len=torrents.length; i<len; ++i )
				o.arguments.ids.push( torrents[i].id() );
		this.sendRequest( o, function( ) {
			remote.loadTorrents();
		} );
	},
	startTorrents: function( torrents ) {
		this.sendTorrentCommand( 'torrent-start', torrents );
	},
	stopTorrents: function( torrents ) {
		this.sendTorrentCommand( 'torrent-stop', torrents );
	},
	removeTorrents: function( torrents ) {
		this.sendTorrentCommand( 'torrent-remove', torrents );
	},
	removeTorrentsAndData: function( torrents ) {
		var remote = this;
		var o = {
			method: 'torrent-remove',
			arguments: {
				'delete-local-data': 'true',
				ids: [ ]
			}
		};

		if( torrents != null )
			for( var i=0, len=torrents.length; i<len; ++i )
				o.arguments.ids.push( torrents[i].id() );

		this.sendRequest( o, function( ) {
			remote.loadTorrents();
		} );
	},
	addTorrentByUrl: function( url, options ) {
		var remote = this;
		var o = {
			method: 'torrent-add',
			arguments: {
				paused: (options.paused ? 'true' : 'false'),
				filename: url
			}
		};
		remote.sendRequest(o, function() {
			remote.loadDaemonPrefs();
		} );
	}
};
