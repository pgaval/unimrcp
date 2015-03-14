

<br>
<h1>0. Status</h1>
Please read the following post before you start:<br>
<br>
<a href='http://groups.google.com/forum/?fromgroups#!topic/unimrcp/yUWi_-FIRqA'>Future Support?</a>

<br>
<h1>1. Overview</h1>
This guide describes how to use Flite TTS with UniMRCP server. It assumes you are already familiar with <a href='InstallationGuide.md'>installation</a> and <a href='ConfigurationGuide.md'>configuration</a> tips of UniMRCP server.<br>
<br>
<br>
<h1>2. Requirements</h1>
<ul><li>UniMRCP <a href='https://code.google.com/p/unimrcp/source/detail?r=1037'>r1037</a> and above<br>
</li><li><a href='http://unimrcp.net/files/flite-1.3.99-latest.tar.gz'>Flite library and voices</a>
<br>
Note: The library and voices are borrowed from FreeSWITCH (<a href='http://svn.freeswitch.org/downloads/libs/'>http://svn.freeswitch.org/downloads/libs/</a>). FreeSWITCH's users can reuse what they have already installed with FreeSWITCH.</li></ul>

<br>
<h1>3. Build</h1>
<h2>GNU Build (Linux and other Unix variants)</h2>
<ol><li>Download Flite library, unpack and build it.<br>
<pre><code>./configure<br>
make<br>
</code></pre></li></ol>


<ol><li>Configure UniMRCP as follows<br>
<pre><code>./configure --enable-flite-plugin --with-flite=path-to-flite-source-dir<br>
make<br>
make install<br>
</code></pre></li></ol>

<br>
<h2>VS2005 Build (Windows XP, Vista, ...)</h2>
<ol><li>Download Flite library, unpack it into unimrcp\libs\flite directory<br>
</li><li>Open flite.sln file and build the solution.<br>
</li><li>Open unimrcp.sln file, include flite project into build (Build -> Configuration Manager).<br>
</li><li>Build the solution.</li></ol>


<br>
<h1>4. Configuration</h1>
Specify mrcpflite as a plugin in unimrcpserver.xml as follows:<br>
<pre><code>    &lt;plugin-factory&gt;<br>
      &lt;engine id="Flite-1" name="mrcpflite" enable="false"/&gt;<br>
    &lt;/plugin-factory&gt;<br>
</code></pre>
There can be multiple plugins of the same resource and it's possible to configure profile based plugins. However, for the beginning, you may want to enable only Flite plugin and disable other TTS plugins.