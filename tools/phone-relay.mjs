// Lets the `pebble` WSL distro reach the watch through the USB-connected phone.
//
// The Pebble (Core) app on the phone has a developer connection on port 9000.
// This script wakes it, forwards it to the PC with adb, and relays that to an
// address WSL can reach, so `pebble install --phone <host>:<port>` works.
//
//   node tools/phone-relay.mjs [relay-port]      (default 9124; Ctrl-C to stop)
//   wsl -d pebble -- bash tools/wsl-build.sh install-phone 172.24.64.1:9124
//
// 172.24.64.1 is the Windows side of WSL's virtual network; `ip route` inside
// the distro shows it if it ever changes. The phone must be unlocked for adb.
import { execFileSync } from 'node:child_process'
import { createServer, connect } from 'node:net'

const ADB = process.env.ADB ?? 'D:/Android/Sdk/platform-tools/adb.exe'
const SERIAL = process.env.ANDROID_SERIAL
const FORWARD_PORT = 9123
const RELAY_PORT = Number(process.argv[2] ?? 9124)
const WSL_HOST = process.env.WSL_HOST ?? '172.24.64.1'

const adb = (...args) => execFileSync(ADB, ['-s', SERIAL, ...args], { encoding: 'utf8' })

adb('shell', 'am', 'broadcast', '-a', 'coredevices.coreapp.DEV_CONNECTION', '-n',
  'coredevices.coreapp/coredevices.coreapp.debug.DevConnectionReceiver')
adb('forward', `tcp:${FORWARD_PORT}`, 'tcp:9000')

createServer((client) => {
  const phone = connect(FORWARD_PORT, '127.0.0.1')
  client.pipe(phone)
  phone.pipe(client)
  const close = () => { client.destroy(); phone.destroy() }
  client.on('error', close)
  phone.on('error', close)
  client.on('close', close)
  phone.on('close', close)
}).listen(RELAY_PORT, WSL_HOST, () => {
  console.log(`relaying ${WSL_HOST}:${RELAY_PORT} -> phone:9000 (adb forward ${FORWARD_PORT})`)
})
