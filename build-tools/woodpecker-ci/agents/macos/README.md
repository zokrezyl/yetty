1. copy the file in this dir to ~/Library/LaunchAgents/com.woodpecker.agent.plist
2. install
```
launchctl load ~/Library/LaunchAgents/com.woodpecker.agent.plist
```
3. start
```
launchctl start com.woodpecker.agent
```

4. check status

```
launchctl list | grep woodpecker
```

5. logs
```
tail -f /tmp/woodpecker-agent.out.log
```
