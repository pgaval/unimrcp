

# 1. Overview #
This guide describes how to configure UniMRCP client and server. It assumes UniMRCP is already installed, otherwise proceed with the [installation](InstallationGuide.md) first. UniMRCP uses XML based configuration for both client and server. The default configuration files [unimrcpclient.xml](http://code.google.com/p/unimrcp/source/browse/trunk/conf/unimrcpclient.xml) and [unimrcpserver.xml](http://code.google.com/p/unimrcp/source/browse/trunk/conf/unimrcpserver.xml) are located in the conf directory of the source tree.
<br>
<br>
<h1>2. Client Configuration</h1>
<h2>2.1. Settings</h2>
<h3>Signaling Agents</h3>
One or more SIP/MRCPv2 and/or RTSP/MRCPv1 signaling agents must be specified. Each signaling agent must be assigned an unique name and a class identifier.<br>
The following is an example of MRCPv2 SofiaSIP signaling agent:<br>
<pre><code>      &lt;agent name="SIP-Agent-1" class="SofiaSIP"&gt;<br>
        &lt;param name="client-ip" value="auto"/&gt;<br>
        &lt;param name="client-port" value="5060"/&gt;<br>
        &lt;param name="server-ip" value="192.168.0.2"/&gt;<br>
        &lt;param name="server-port" value="5060"/&gt;<br>
        &lt;param name="sip-transport" value="udp"/&gt;<br>
      &lt;/agent&gt;<br>
</code></pre>
The following is an example of MRCPv1 UniRTSP signaling agent:<br>
<pre><code>      &lt;agent name="RTSP-Agent-1" class="UniRTSP"&gt;<br>
        &lt;param name="server-ip" value="192.168.0.2"/&gt;<br>
        &lt;param name="server-port" value="1554"/&gt;<br>
        &lt;param name="resource-location" value="media"/&gt;<br>
      &lt;/agent&gt;<br>
</code></pre>

<h3>Connection Agents (MRCPv2 only)</h3>
One or more MRCPv2 connection agents must be specified. Each connection agent must be assigned an unique name. The following is an example of MRCPv2 connection agent:<br>
<pre><code>      &lt;agent name="MRCPv2-Agent-1"&gt;<br>
        &lt;param name="max-connection-count" value="100"/&gt;<br>
      &lt;/agent&gt;<br>
</code></pre>

<h3>Media Engines</h3>
One or more media processing engines must be specified. Each media engine must be assigned an unique name. No media engine might be specified in case external media processing is used (bypass media mode). The following is an example of media engine:<br>
<pre><code>      &lt;engine name="Media-Engine-1"/&gt;<br>
</code></pre>

<h3>RTP Termination Factories</h3>
One or more RTP termination factories must be specified. Each factory must be assigned an unique name. No factory might be specified in case external media processing is used (bypass media mode). The following is an example of RTP termination factory:<br>
<pre><code>      &lt;rtp name="RTP-Factory-1"&gt;<br>
        &lt;param name="rtp-ip" value="auto"/&gt;<br>
        &lt;param name="rtp-port-min" value="4000"/&gt;<br>
        &lt;param name="rtp-port-max" value="5000"/&gt;<br>
        &lt;!-- &lt;param name="playout-delay" value="50"/&gt; --&gt;<br>
        &lt;!-- &lt;param name="max-playout-delay" value="200"/&gt; --&gt;<br>
        &lt;!-- &lt;param name="ptime" value="20"/&gt; --&gt;<br>
        &lt;!-- &lt;param name="codecs" value="PCMU PCMA L16/96/8000"/&gt; --&gt;<br>
      &lt;/rtp&gt;<br>
</code></pre>

Note: By default "auto" directive is used wherever the local IP address to bind to must be specified. In this case IP address is automatically guessed. However it may cause some delay on startup. You may want to explicitly specify the IP address in order to speedup the startup or in case the guessed IP address is incorrect.<br>
<br>
<h2>2.2. Profiles</h2>
Profiles are collection of settings. Each profile includes one of signaling, connection agents, etc. Each profile must be assigned an unique name to be referenced by. Multiple profiles can be used at the same time to provide connectivity to several MRCPv2 and/or MRCPv1 servers. It's up to actual application based on UniMRCP client to specify which of the existing profiles to use upon creation of new MRCP session.<br>
The following is an example of MRCPv2 profile:<br>
<pre><code>    &lt;profile name="MRCPv2-Default"&gt;<br>
      &lt;param name="signaling-agent" value="SIP-Agent-1"/&gt;<br>
      &lt;param name="connection-agent" value="MRCPv2-Agent-1"/&gt;<br>
      &lt;param name="media-engine" value="Media-Engine-1"/&gt;<br>
      &lt;param name="rtp-factory" value="RTP-Factory-1"/&gt;<br>
    &lt;/profile&gt;<br>
</code></pre>

The following is an example of MRCPv1 profile:<br>
<pre><code>    &lt;profile name="MRCPv1-Default"&gt;<br>
      &lt;param name="signaling-agent" value="RTSP-Agent-1"/&gt;<br>
      &lt;param name="media-engine" value="Media-Engine-1"/&gt;<br>
      &lt;param name="rtp-factory" value="RTP-Factory-1"/&gt;<br>
    &lt;/profile&gt;<br>
<br>
</code></pre>
<br>
<br>
<h1>3. Server Configuration</h1>
<h2>3.1. Settings</h2>
<h3>Signaling Agents</h3>
One or more SIP/MRCPv2 and/or RTSP/MRCPv1 signaling agents must be specified. Each signaling agent must be assigned an unique name and a class identifier.<br>
The following is an example of MRCPv2 SofiaSIP signaling agent:<br>
<pre><code>      &lt;agent name="SIP-Agent-1" class="SofiaSIP"&gt;<br>
        &lt;param name="sip-ip" value="auto"/&gt;<br>
        &lt;param name="sip-port" value="5060"/&gt;<br>
        &lt;param name="sip-transport" value="udp,tcp"/&gt;<br>
      &lt;/agent&gt;<br>
</code></pre>
The following is an example of MRCPv1 UniRTSP signaling agent:<br>
<pre><code>      &lt;agent name="RTSP-Agent-1" class="UniRTSP"&gt;<br>
        &lt;param name="rtsp-ip" value="auto"/&gt;<br>
        &lt;param name="rtsp-port" value="1554"/&gt;<br>
      &lt;/agent&gt;<br>
</code></pre>

<h3>Connection Agents (MRCPv2 only)</h3>
One or more MRCPv2 connection agents must be specified. Each connection agent must be assigned an unique name. The following is an example of MRCPv2 connection agent:<br>
<pre><code>      &lt;agent name="MRCPv2-Agent-1"&gt;<br>
        &lt;param name="mrcp-ip" value="auto"/&gt;<br>
        &lt;param name="mrcp-port" value="1544"/&gt;<br>
        &lt;param name="max-connection-count" value="100"/&gt;<br>
      &lt;/agent&gt;<br>
</code></pre>

<h3>Media Engines</h3>
One or more media processing engines must be specified. Each media engine must be assigned an unique name. The following is an example of media engine:<br>
<pre><code>      &lt;engine name="Media-Engine-1"/&gt;<br>
</code></pre>

<h3>RTP Termination Factories</h3>
One or more RTP termination factories can be specified. Each factory must be assigned an unique name. The following is an example of RTP termination factory:<br>
<pre><code>      &lt;rtp name="RTP-Factory-1"&gt;<br>
        &lt;param name="rtp-ip" value="auto"/&gt;<br>
        &lt;param name="rtp-port-min" value="5000"/&gt;<br>
        &lt;param name="rtp-port-max" value="6000"/&gt;<br>
        &lt;!-- &lt;param name="playout-delay" value="50"/&gt; --&gt;<br>
        &lt;!-- &lt;param name="max-playout-delay" value="200"/&gt; --&gt;<br>
      &lt;/rtp&gt;<br>
</code></pre>

<h3>MRCP Resource Engines (Plugins)</h3>
Engines provide back-end for the MRCP resources (synthesizer,recognizer,...) implementing UniMRCP abstract resource engine interface. Engines are integrated into UniMRCP server as plugins. One or more engines (plugins) must be provided to every MRCP resource. Each engine must be assigned an unique name and a class identifier. Class identifier is used to construct the name of the plugin. The way name of the plugin is constructed and the directory where the plugin is looked for are platform dependent. For instance, if class identifier of an engine is "demosynth", the name of the plugin would be "demosynth.dll" for Windows and "demosynth.so" for Linux.<br>
The following example specifies two demo engines (plugins):<br>
<pre><code>    &lt;plugin&gt;<br>
      &lt;engine name="Demo-Synth-1" class="demosynth" enable="1"/&gt;<br>
      &lt;engine name="Demo-Recog-1" class="demorecog" enable="1"/&gt;<br>
    &lt;/plugin&gt;<br>
</code></pre>

Note: By default "auto" directive is used wherever the local IP address to bind to must be specified. In this case IP address is automatically guessed. However it may cause some delay on startup. You may want to explicitly specify the IP address in order to speedup the startup or in case the guessed IP address is incorrect.<br>
<br>
<h2>3.2. Profiles</h2>
Profiles are collection of settings. Each profile includes one of signaling, connection agents, etc. Each profile must be assigned an unique name to be referenced by. Multiple profiles may exist. One profile may provide MRCPv2 capabilities, the second - MRCPv1, while yet others - MRCPv2 or MRCPv1 with somewhat different settings. It depends on the signaling agent MRCP session is established by which profile to use.<br>
The following is an example of MRCPv2 profile:<br>
<pre><code>    &lt;profile name="MRCPv2-Default"&gt;<br>
      &lt;param name="signaling-agent" value="SIP-Agent-1"/&gt;<br>
      &lt;param name="connection-agent" value="MRCPv2-Agent-1"/&gt;<br>
      &lt;param name="media-engine" value="Media-Engine-1"/&gt;<br>
      &lt;param name="rtp-factory" value="RTP-Factory-1"/&gt;<br>
    &lt;/profile&gt;<br>
</code></pre>

The following is an example of MRCPv1 profile:<br>
<pre><code>    &lt;profile name="MRCPv1-Default"&gt;<br>
      &lt;param name="signaling-agent" value="RTSP-Agent-1"/&gt;<br>
      &lt;param name="media-engine" value="Media-Engine-1"/&gt;<br>
      &lt;param name="rtp-factory" value="RTP-Factory-1"/&gt;<br>
    &lt;/profile&gt;<br>
</code></pre>