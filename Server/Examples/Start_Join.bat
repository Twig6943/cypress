set PLAYERNAME=Player
set IPADDRESS=0.0.0.0:25200

start GW2.Main_Win64_Retail.exe ^
-playerName %PLAYERNAME% ^
-console ^
#-password password goes here if the server has a password ^
-runMultipleGameInstances ^
-Client.ServerIp %IPADDRESS% ^
#-dataPath "ModData\Default"
