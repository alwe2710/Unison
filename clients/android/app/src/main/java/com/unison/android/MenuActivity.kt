package com.unison.android

import android.content.Intent
import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.HttpURLConnection
import java.net.InetSocketAddress
import java.net.SocketTimeoutException
import java.net.URL
import org.json.JSONObject

private enum class SlotState { UNKNOWN, FREE, OCCUPIED, UNREACHABLE }

/** One server seen via the UDP discovery beacon (docs/protocol.md,
 * "Discovery-Beacon (UDP)"). `compatible` mirrors the beacon's
 * protocol_version against GbaStreamClient.PROTOCOL_VERSION -- an
 * incompatible entry is still shown (the server *is* there), just not
 * selectable, matching the doc's "in der Serverliste erkennbar markieren"
 * requirement. */
private data class DiscoveredServer(
    val host: String,
    val emulatorIdentifier: String,
    val gameTitle: String,
    val streamType: String,
    val handshakePort: Int,
    val protocolVersion: Int,
    val compatible: Boolean
)

/**
 * Landing screen (three-page app: menu -> settings / player). Two ways to
 * find a host:
 *
 * 1. Manual host entry + "Suchen" -- a bare host assumes GC_GBA_LINK (poll
 *    GET /status on all four player ports, docs/protocol.md), since there's
 *    no beacon to read a stream_type from for a host the user just typed
 *    in. Typing "host:port" instead skips that probe and connects straight
 *    to that port, for single-slot stream types (Cemu/Azahar/melonDS) that
 *    don't speak the GC_GBA_LINK lobby endpoint at all.
 * 2. Discovery: listens for the UDP beacon every Unison server broadcasts
 *    (docs/protocol.md, "Discovery-Beacon (UDP)"). Tapping a discovered
 *    GC_GBA_LINK entry funnels into the same P1-P4 picker as manual entry;
 *    every other stream type (N3DS_BOTTOM_SCREEN, NDS_BOTTOM_SCREEN,
 *    WIIU_GAMEPAD, ...) is single-client, so it connects straight to the
 *    beacon's handshake_port instead -- probing PLAYER_BASE_PORT+0..3
 *    against a server that was never Dolphin doesn't find "free" or
 *    "occupied" slots, just four unreachable ports.
 *
 * Picking a free P slot (or a direct-connect discovery entry) starts
 * PlayerActivity; the settings button opens SettingsActivity. Neither owns
 * any GbaStreamClient/native state -- that's entirely PlayerActivity's job.
 *
 * No fixed orientation (see AndroidManifest.xml): this is a form, not the
 * stream view, so it should follow however the device is actually held.
 */
@OptIn(ExperimentalMaterial3Api::class)
class MenuActivity : LocalizedActivity() {

    private var hostText by mutableStateOf("")
    private var searching by mutableStateOf(false)
    private var pickerVisible by mutableStateOf(false)
    private var slotStates by mutableStateOf(List(GbaStreamClient.PLAYER_SLOT_COUNT) { SlotState.UNKNOWN })
    private var statusText by mutableStateOf("")

    private var discovering by mutableStateOf(false)
    private var discoveryProgress by mutableStateOf(0f)
    private var discoveryStatusText by mutableStateOf("")
    private val discoveredServers = mutableStateListOf<DiscoveredServer>()

    private var lastSearchedHost: String? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        statusText = getString(R.string.status_disconnected)

        setContent {
            UnisonTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    Scaffold(
                        topBar = {
                            TopAppBar(
                                title = {
                                    Row(verticalAlignment = Alignment.CenterVertically) {
                                        Image(
                                            painter = painterResource(R.drawable.unison_logo),
                                            contentDescription = null,
                                            modifier = Modifier.size(32.dp)
                                        )
                                        Spacer(Modifier.width(8.dp))
                                        Text(stringResource(R.string.app_name))
                                    }
                                },
                                actions = {
                                    TextButton(onClick = {
                                        startActivity(Intent(this@MenuActivity, SettingsActivity::class.java))
                                    }) {
                                        Text(stringResource(R.string.settings))
                                    }
                                }
                            )
                        }
                    ) { innerPadding ->
                        Column(modifier = Modifier.padding(innerPadding).padding(16.dp).fillMaxSize()) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                OutlinedTextField(
                                    value = hostText,
                                    onValueChange = { hostText = it },
                                    label = { Text(stringResource(R.string.host_hint)) },
                                    singleLine = true,
                                    modifier = Modifier.weight(1f)
                                )
                                Spacer(Modifier.width(8.dp))
                                Button(onClick = { searchLobby() }, enabled = !searching) {
                                    Text(stringResource(R.string.menu_connect))
                                }
                            }

                            if (pickerVisible) {
                                Spacer(Modifier.height(12.dp))
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                                ) {
                                    slotStates.forEachIndexed { index, state ->
                                        Button(
                                            onClick = {
                                                lastSearchedHost?.let {
                                                    launchPlayer(
                                                        it,
                                                        GbaStreamClient.PLAYER_BASE_PORT + index,
                                                        GbaStreamClient.STREAM_TYPE_GC_GBA_LINK
                                                    )
                                                }
                                            },
                                            enabled = state == SlotState.FREE,
                                            modifier = Modifier.weight(1f)
                                        ) {
                                            Text("P${index + 1}")
                                        }
                                    }
                                }
                            }

                            Spacer(Modifier.height(8.dp))
                            Text(statusText, style = MaterialTheme.typography.bodyMedium)

                            Spacer(Modifier.height(16.dp))
                            HorizontalDivider()
                            Spacer(Modifier.height(16.dp))

                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(stringResource(R.string.discovery_title), modifier = Modifier.weight(1f))
                                Button(onClick = { startDiscovery() }, enabled = !discovering) {
                                    Text(stringResource(R.string.discovery_start))
                                }
                            }
                            Spacer(Modifier.height(4.dp))
                            if (discovering) {
                                LinearProgressIndicator(
                                    progress = { discoveryProgress },
                                    modifier = Modifier.fillMaxWidth()
                                )
                                Spacer(Modifier.height(4.dp))
                            }
                            Text(
                                discoveryStatusText,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )

                            Spacer(Modifier.height(8.dp))
                            LazyColumn(modifier = Modifier.weight(1f)) {
                                items(discoveredServers) { server ->
                                    TextButton(
                                        onClick = {
                                            if (server.compatible) {
                                                if (server.streamType == GbaStreamClient.STREAM_TYPE_GC_GBA_LINK) {
                                                    hostText = server.host
                                                    runSearch(server.host)
                                                } else {
                                                    launchPlayer(server.host, server.handshakePort, server.streamType)
                                                }
                                            }
                                        },
                                        enabled = server.compatible,
                                        modifier = Modifier.fillMaxWidth()
                                    ) {
                                        Column(modifier = Modifier.fillMaxWidth()) {
                                            val title = if (server.gameTitle.isNotEmpty()) {
                                                "${server.emulatorIdentifier}: ${server.gameTitle}"
                                            } else {
                                                server.emulatorIdentifier
                                            }
                                            Text("${server.host} — $title")
                                            if (!server.compatible) {
                                                Text(
                                                    getString(
                                                        R.string.discovery_incompatible,
                                                        server.protocolVersion,
                                                        GbaStreamClient.PROTOCOL_VERSION
                                                    ),
                                                    style = MaterialTheme.typography.bodySmall,
                                                    color = MaterialTheme.colorScheme.error
                                                )
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    private fun launchPlayer(host: String, port: Int, streamType: String) {
        val intent = Intent(this, PlayerActivity::class.java)
        intent.putExtra(PlayerActivity.EXTRA_HOST, host)
        intent.putExtra(PlayerActivity.EXTRA_PORT, port)
        intent.putExtra(PlayerActivity.EXTRA_STREAM_TYPE, streamType)
        startActivity(intent)
    }

    // --- Manual entry + P1-P4 picker (plain HTTP, not through unison_core:
    // GET /status isn't part of the stream protocol). ---

    private fun searchLobby() {
        val raw = hostText.trim()
        if (raw.isEmpty()) {
            statusText = getString(R.string.status_error, getString(R.string.lobby_host_required))
            return
        }

        // A typed "host:port" means "connect directly to this single-slot
        // Unison server" (Cemu/Azahar/melonDS's WIIU_GAMEPAD/
        // N3DS_BOTTOM_SCREEN/NDS_BOTTOM_SCREEN stream types) -- those don't
        // speak GC_GBA_LINK's GET /status lobby endpoint at all, and each
        // uses its own configured port instead of PLAYER_BASE_PORT+0..3, so
        // there's nothing to probe: launch straight into PlayerActivity,
        // same as a discovered non-GC_GBA_LINK beacon entry already does
        // (see the LazyColumn's onClick above). A bare host (no colon) keeps
        // the original GC_GBA_LINK lobby-probe behavior unchanged.
        val colonIndex = raw.lastIndexOf(':')
        if (colonIndex > 0) {
            val port = raw.substring(colonIndex + 1).toIntOrNull()
            if (port != null) {
                // Unlike the two paths above, the actual stream_type here is
                // genuinely unknown until the handshake's own hello message
                // arrives (no beacon, no lobby probe) -- passed through as
                // "" (Prefs.bilinearFor("") -- and every other client's own
                // manual-entry path has the same limitation for whatever
                // stream-type-dependent behavior it has.
                launchPlayer(raw.substring(0, colonIndex), port, "")
                return
            }
        }

        runSearch(raw)
    }

    private fun runSearch(host: String) {
        hostText = host
        searching = true
        pickerVisible = false
        statusText = getString(R.string.lobby_searching)

        Thread {
            val occupied = (0 until GbaStreamClient.PLAYER_SLOT_COUNT).map { slot ->
                fetchOccupied(host, GbaStreamClient.PLAYER_BASE_PORT + slot)
            }
            runOnUiThread { applyLobbyResults(host, occupied) }
        }.start()
    }

    /** null = unreachable (port not configured as GBA (Client-Stream) at all, or unrelated error). */
    private fun fetchOccupied(host: String, port: Int): Boolean? {
        var connection: HttpURLConnection? = null
        return try {
            connection = (URL("http://$host:$port/status").openConnection() as HttpURLConnection).apply {
                connectTimeout = 1500
                readTimeout = 1500
                requestMethod = "GET"
            }
            val body = connection.inputStream.bufferedReader().use { it.readText() }
            JSONObject(body).optBoolean("occupied", false)
        } catch (e: Exception) {
            null
        } finally {
            connection?.disconnect()
        }
    }

    private fun applyLobbyResults(host: String, occupied: List<Boolean?>) {
        lastSearchedHost = host
        searching = false

        var anyFree = false
        slotStates = occupied.map { value ->
            when (value) {
                false -> { anyFree = true; SlotState.FREE }
                true -> SlotState.OCCUPIED
                null -> SlotState.UNREACHABLE
            }
        }
        pickerVisible = true
        statusText = if (anyFree) getString(R.string.lobby_pick) else getString(R.string.lobby_none_free)
    }

    // --- Discovery: listen for the UDP broadcast beacon every Unison
    // server sends every 2s (docs/protocol.md, "Discovery-Beacon (UDP)") --
    // a real discovery protocol, unlike the subnet sweep this replaced,
    // which relied on the lobby port answering plain HTTP GET with an HTML
    // page. Since protocol_version 2 that page is gone (the lobby speaks the
    // app-level handshake instead), so that sweep could no longer find
    // anything -- this listens for what the server now actually announces. ---

    private fun startDiscovery() {
        discovering = true
        discoveryProgress = 0f
        discoveredServers.clear()
        discoveryStatusText = getString(R.string.discovery_scanning)

        Thread {
            val seenHosts = HashSet<String>()
            var socket: DatagramSocket? = null
            try {
                socket = DatagramSocket(null).apply {
                    reuseAddress = true
                    bind(InetSocketAddress(GbaStreamClient.BEACON_PORT))
                    soTimeout = 200 // poll interval, so progress/deadline stay responsive
                }

                val buf = ByteArray(2048)
                val deadline = System.currentTimeMillis() + DISCOVERY_DURATION_MS
                while (System.currentTimeMillis() < deadline) {
                    runOnUiThread {
                        val remaining = (deadline - System.currentTimeMillis()).coerceAtLeast(0)
                        discoveryProgress = 1f - (remaining.toFloat() / DISCOVERY_DURATION_MS)
                    }

                    val packet = DatagramPacket(buf, buf.size)
                    try {
                        socket.receive(packet)
                    } catch (e: SocketTimeoutException) {
                        continue
                    }

                    val json = try {
                        JSONObject(String(packet.data, 0, packet.length, Charsets.UTF_8))
                    } catch (e: Exception) {
                        continue // not JSON at all -- unrelated UDP traffic on this port
                    }
                    if (json.optString("type") != "unison_beacon") continue

                    val beaconHost = json.optString("host")
                    val host = beaconHost.ifEmpty { packet.address?.hostAddress ?: "" }
                    if (host.isEmpty()) continue
                    if (!seenHosts.add(host)) continue // already listed from an earlier beacon tick

                    val protocolVersion = json.optInt("protocol_version", -1)
                    val server = DiscoveredServer(
                        host = host,
                        emulatorIdentifier = json.optString("emulator_identifier", "?"),
                        gameTitle = json.optString("game_title", ""),
                        streamType = json.optString("stream_type", ""),
                        handshakePort = json.optInt("handshake_port", 0),
                        protocolVersion = protocolVersion,
                        compatible = protocolVersion == GbaStreamClient.PROTOCOL_VERSION
                    )
                    runOnUiThread { discoveredServers.add(server) }
                }
            } catch (e: Exception) {
                // Bind failure (port in use, no network) -- surfaced below via
                // an empty result, same as "listened but heard nothing".
            } finally {
                socket?.close()
            }

            runOnUiThread {
                discovering = false
                discoveryProgress = 1f
                discoveryStatusText = if (seenHosts.isEmpty()) {
                    getString(R.string.discovery_none_found)
                } else {
                    getString(R.string.discovery_found, seenHosts.size)
                }
            }
        }.start()
    }

    companion object {
        // Long enough to reliably catch at least one beacon tick (every 2s,
        // docs/protocol.md) even with some jitter, short enough that
        // "Suchen" still feels responsive.
        private const val DISCOVERY_DURATION_MS = 3500L
    }
}
