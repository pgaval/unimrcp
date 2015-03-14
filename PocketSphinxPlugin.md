

<br>
<h1>0. Status</h1>
Please read the following post before you start:<br>
<br>
<a href='http://groups.google.com/forum/?fromgroups#!topic/unimrcp/yUWi_-FIRqA'>Future Support?</a>

<br>
<h1>1. Overview</h1>
This guide describes how to use PocketSphinx ASR with UniMRCP server. It assumes you are already familiar with <a href='InstallationGuide.md'>installation</a> and <a href='ConfigurationGuide.md'>configuration</a> tips of UniMRCP server.<br>
<br>
<br>
<h1>2. Requirements</h1>
<ul><li>UniMRCP <a href='https://code.google.com/p/unimrcp/source/detail?r=1030'>r1030</a> and above<br>
</li><li><a href='http://www.unimrcp.org/dependencies/sphinxbase-0.4.99-latest.tar.gz'>SphinxBase library</a>
</li><li><a href='http://www.unimrcp.org/dependencies/pocketsphinx-0.5.99-latest.tar.gz'>PocketSphinx library</a>
</li><li><a href='http://www.unimrcp.org/dependencies/communicator_semi_6000_20080321.tar.gz'>PocketSphinx model</a>
<br>
Note: The libraries and model are borrowed from FreeSWITCH (<a href='http://svn.freeswitch.org/downloads/libs/'>http://svn.freeswitch.org/downloads/libs/</a>). FreeSWITCH's users can reuse what they have already installed with FreeSWITCH.</li></ul>

<br>
<h1>3. Build</h1>
<h2>GNU Build (Linux and other Unix variants)</h2>
<ol><li>Download SphinxBase and PocketSphinx libraries, unpack and build them. It's not required to install these libraries.<br>
<pre><code>./configure<br>
make<br>
</code></pre></li></ol>


<ol><li>Configure UniMRCP as follows<br>
<pre><code>./configure --enable-pocketsphinx-plugin --with-pocketsphinx=path-to-pocketsphinx --with-sphinxbase=path-to-sphinxbase<br>
make<br>
make install<br>
</code></pre>
</li><li>Download PocketSphinx model (communicator), unpack it to InstallDir/data next to wsj1 model and default.dic, which should be already there.</li></ol>

<br>
<h2>VS2005 Build (Windows XP, Vista, ...)</h2>
<ol><li>Download SphinxBase and PocketSphinx libraries, unpack them into unimrcp\libs directory as follows<br>
<ul><li>sphinxbase<br>
</li><li>pocketsphinx<br>
</li></ul></li><li>Open sphinxbase.sln file and build the solution.<br>
</li><li>Open pocketsphinx.sln file and build the solution.<br>
</li><li>Open unimrcp.sln file, include mrcppocketsphinx project into build (Build -> Configuration Manager).<br>
</li><li>Build the solution.<br>
</li><li>Build utility projects to copy all the required data to output directory (prepare and/or preparesphinx in Solution Explorer -> tools).<br>
</li><li>Download PocketSphinx model (communicator), unpack it to OutputDir\data next to wsj1 model and default.dic, which should be already there.</li></ol>


<br>
<h1>4. Configuration</h1>
Specify mrcppocketsphinx as a plugin in unimrcpserver.xml as follows:<br>
<pre><code>    &lt;plugin&gt;<br>
      &lt;engine name="PocketSphinx-1" class="mrcppocketsphinx" enable="1"/&gt;<br>
    &lt;/plugin&gt;<br>
</code></pre>
There can be multiple plugins of the same resource and it's possible to configure profile based plugins. However for the beginning you may want to enable only PocketSphinx ASR plugin and disable other ASR plugins.<br>
<br>
PocketSphinx recognition related properties can be configured via pocketsphinx.xml<br>
<pre><code>&lt;!-- PocketSphinx Document --&gt;<br>
&lt;pocketsphinx&gt;<br>
  &lt;!-- Senstitivity level and timeout are used for voice activity (speech) detection <br>
       reasonable values: level is in [2..250] range, timeout is in [100..500] msec range<br>
  --&gt;<br>
  &lt;sensitivity level="50" timeout="300"/&gt;<br>
<br>
  &lt;!-- Default values for noinput and recognition timeout,<br>
       these values can be overriden by client's requests<br>
  --&gt;<br>
  &lt;timers noinput-timeout="10000" recognition-timeout="15000"/&gt;<br>
<br>
<br>
  &lt;!-- PocketSphinx related model and dictionary data.<br>
       Default dir (dir="") is InstallDir/data<br>
  --&gt;<br>
  &lt;model dir="" narrowband="communicator" wideband="wsj1" dictionary="default.dic" preferred="narrowband"/&gt;<br>
  &lt;!-- &lt;model dir="/usr/local/freeswitch/grammar" narrowband="model/communicator" wideband="model/wsj1" dictionary="default.dic"/&gt; --&gt;<br>
<br>
  &lt;!-- Enable to save utterance.<br>
       Default dir (dir="") is InstallDir/data<br>
  --&gt;<br>
  &lt;save-waveform dir="" enable="0"/&gt;<br>
&lt;/pocketsphinx&gt;<br>
</code></pre>