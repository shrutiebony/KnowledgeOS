# KnowledgeOS on one KVM guest

The C++ server, scheduler, CPU workers, SQLite file, dashboard, and Gemini adapter run **inside a single Linux VM**. Wikipedia, GDELT, and the Gemini API stay outside the guest, reached over the VM network.

KVM sets CPU, RAM, and disk limits. It does not change the analysis math.

## Guest install

On Ubuntu in the VM, with this repository at `/opt/knowledgeos`:

```bash
sudo bash /opt/knowledgeos/deploy/kvm/install-guest.sh /opt/knowledgeos
```

Put `GEMINI_API_KEY` in `/etc/knowledgeos/knowledgeos.env` (mode 600) only if you want Ask / Explain. You may also set `GEMINI_MODEL`. Metrics and **Explain insights** work without it. Do not commit the key.

SQLite lives on a persistent disk at `/var/lib/knowledgeos/data/knowledgeos.db`.

Expose the dashboard with a host port forward or reverse proxy to guest `:8080`.

## Host launch helper

`launch-vm.sh` is a starting `virt-install` wrapper. Set `KOS_CLOUD_IMAGE` to a cloud-init disk image. After boot, mount the project into `/opt/knowledgeos` and run `install-guest.sh`.

Do not split workers across VMs with the current SQLite scheduler.
