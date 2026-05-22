import os
from time import sleep

from mininet.log import info, setLogLevel

from minindn.apps.app_manager import AppManager
from minindn.apps.application import Application
from minindn.apps.nfd import Nfd
from minindn.apps.nlsr import Nlsr
from minindn.minindn import Minindn
from minindn.util import MiniNDNCLI


class WeaverNlsr(Nlsr):
    def createConfigFile(self):
        super().createConfigFile()
        self._sanitize_advertising_section()

    def _sanitize_advertising_section(self):
        with open(self.confFile, 'r', encoding='utf-8') as f:
            lines = f.readlines()

        sanitized = []
        in_advertising = False
        for line in lines:
            stripped = line.strip()
            if stripped == 'advertising':
                in_advertising = True
                sanitized.append(line)
                continue

            if in_advertising and stripped == '}':
                in_advertising = False
                sanitized.append(line)
                continue

            if in_advertising and stripped.startswith('prefix '):
                prefix = stripped.split(None, 1)[1]
                sanitized.append(f'    {prefix} 0\n')
                continue

            if in_advertising and stripped.startswith('/'):
                continue

            sanitized.append(line)

        with open(self.confFile, 'w', encoding='utf-8') as f:
            f.writelines(sanitized)


def start_app(host, command, log_name):
    info(f'Starting on {host.name}: {command}\n')
    app = Application(host)
    app.start(command, log_name)
    return app


if __name__ == '__main__':
    setLogLevel('info')

    script_dir = os.path.dirname(os.path.abspath(__file__))
    overlay_root = os.path.abspath(os.path.join(script_dir, '..'))
    topology = os.path.join(overlay_root, 'topologies', 'weaver-simple.conf')
    weaverd = os.path.join(overlay_root, 'weaverapps', 'weaverd')
    payload_dir = os.environ.get('WEAVER_PAYLOAD_DIR', '')
    output_dir = os.environ.get('WEAVER_OUTPUT_DIR', os.path.join(overlay_root, 'logs'))
    iterations = int(os.environ.get('WEAVER_ITERATIONS', '20'))
    runtime = int(os.environ.get('WEAVER_RUNTIME', '60'))
    no_cli = os.environ.get('WEAVER_NO_CLI', '0') == '1'
    os.makedirs(os.path.join(overlay_root, 'logs'), exist_ok=True)
    os.makedirs(output_dir, exist_ok=True)

    Minindn.cleanUp()
    Minindn.verifyDependencies()

    ndn = Minindn(topoFile=topology)
    ndn.start()

    info('Starting NFD\n')
    AppManager(ndn, ndn.net.hosts, Nfd)

    info('Starting NLSR\n')
    AppManager(ndn, ndn.net.hosts, WeaverNlsr)
    sleep(20)

    con0 = ndn.net['con0']
    agg0 = ndn.net['agg0']
    agg1 = ndn.net['agg1']
    producers = [ndn.net[f'pro{i}'] for i in range(4)]

    apps = []
    for pro in producers:
        trace = os.path.join(overlay_root, 'logs', f'{pro.name}-trace.csv')
        payload_arg = f' --payload-dir {payload_dir}' if payload_dir else ''
        apps.append(start_app(
            pro,
            f'{weaverd} --role producer --prefix /{pro.name} --value 1 --trace {trace}{payload_arg}',
            f'{pro.name}-weaver.log'))
        sleep(1)

    apps.append(start_app(
        agg0,
        f'{weaverd} --role aggregator --prefix /agg0 --children /pro0,/pro1 '
        f'--trace {os.path.join(overlay_root, "logs", "agg0-trace.csv")}',
        'agg0-weaver.log'))
    sleep(1)

    apps.append(start_app(
        agg1,
        f'{weaverd} --role aggregator --prefix /agg1 --children /pro2,/pro3 '
        f'--trace {os.path.join(overlay_root, "logs", "agg1-trace.csv")}',
        'agg1-weaver.log'))
    sleep(1)

    info('Advertising producer and aggregator prefixes\n')
    for node in producers + [agg0, agg1]:
        node.cmd(f'nlsrc advertise /{node.name}')
        sleep(1)

    sleep(10)

    apps.append(start_app(
        con0,
        f'{weaverd} --role root --prefix /con0 --children /agg0,/agg1 '
        f'--iterations {iterations} --cc AIMD --timeout-ms 1000 '
        f'--trace {os.path.join(overlay_root, "logs", "con0-trace.csv")} '
        f'--output-dir {output_dir}',
        'con0-weaver.log'))

    sleep(runtime)
    if not no_cli:
        MiniNDNCLI(ndn.net)

    for app in apps:
        app.stop()
    ndn.stop()
