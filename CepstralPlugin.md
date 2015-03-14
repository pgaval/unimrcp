

# 1. Overview #
This guide describes how to use Cepstral Swift TTS with UniMRCP server. It assumes you are already familiar with [installation](InstallationGuide.md) and [configuration](ConfigurationGuide.md) tips of UniMRCP server.
UniMRCP server provides plugin interface to integrate 3-rd party TTS and ASR engines. Plugin called mrcpcepstral implements the MRCP resource engine interface using Cepstral Swift TTS engine.

# 2. Requirements #
Cepstral Swift SDK and Voices must be installed first.
http://support.cepstral.com/

# 3. Build #
## GNU Build (Linux and other Unix variants) ##
### Configure option ###
> To explicitly specify where to look for the Cepstral Swift library use the "--with-swift=" option:
> > $ ./configure --with-swift=/opt/swift

## VS2005 Build (Windows XP, Vista, ...) ##
### One-time pre-build preparation ###
  * To specify where to look for the Swift library use cepstral.vsprops located in build/vsprops:
```
       <UserMacro
           Name="CepstralSDKDir"
           Value="C:\Program Files\Cepstral\sdk"
       />
```

Open unimrcp.sln solution file, enable mrcpcepstral project from Build -> Configuration Menu and build the solution.
Make sure swift.dll is in the execution path of unimrcpserver in case mrcpcepstral.dll failed to load on start-up.

# 4. Configuration #
Specify mrcpcepstral as a plugin in unimrcpserver.xml as follows:
```
    <plugin>
      <engine name="Cepstral-Swift-1" class="mrcpcepstral" enable="1"/>
    </plugin>
```