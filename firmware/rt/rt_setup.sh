#!/usr/bin/env bash
# rt_setup.sh — PREEMPT_RT 主站機一鍵調校（WP-L0.4;方案 B/C 同用）
#
# 依 docs/design/linux-rt-ethercat-master-plan.md §3.2：
#   內核參數（grub）：isolcpus/nohz_full/rcu_nocbs/irqaffinity/skew_tick
#                     intel_pstate=disable/max_cstate/nosoftlockup
#   執行期：governor=performance、關 turbo、NIC/CAN IRQ 綁核、
#           /dev/cpu_dma_latency=0（需常駐 holder,見 --hold-dma）
#   檢查：PREEMPT_RT?、隔離核生效?、cyclictest/hwlatdetect 提示
#
# 用法（冪等,重跑安全;預設 dry-run 只印不做）：
#   sudo ./rt_setup.sh --cores 2,3 --nic enp3s0 [--apply] [--grub] [--check]
#     --cores A,B   隔離核心（IRQ 核 A,cyclic 核 B;預設 2,3）
#     --nic IF      EtherCAT 專用網卡（IRQ 綁到第一個隔離核）
#     --can IF      （方案 C）CAN 介面,IRQ 綁法相同
#     --apply       真的執行執行期調校（預設只印命令）
#     --grub        產生 grub 參數並寫入 /etc/default/grub.d/（要重開機）
#     --check       只跑驗收檢查
#     --hold-dma    常駐持有 /dev/cpu_dma_latency=0（前景,建議 systemd 化）
set -u

CORES="2,3"; NIC=""; CANIF=""; APPLY=0; DO_GRUB=0; DO_CHECK=0; HOLD=0
while [ $# -gt 0 ]; do
    case $1 in
        --cores) CORES=$2; shift 2 ;;
        --nic)   NIC=$2; shift 2 ;;
        --can)   CANIF=$2; shift 2 ;;
        --apply) APPLY=1; shift ;;
        --grub)  DO_GRUB=1; shift ;;
        --check) DO_CHECK=1; shift ;;
        --hold-dma) HOLD=1; shift ;;
        *) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
    esac
done
IRQ_CORE=${CORES%%,*}
CYC_CORE=${CORES##*,}
NCPU=$(nproc --all)
HOUSE=$(seq 0 $((NCPU-1)) | grep -vE "^(${IRQ_CORE}|${CYC_CORE})$" | paste -sd, -)

run() { if [ "$APPLY" = 1 ]; then echo "+ $*"; eval "$*"; else echo "DRY + $*"; fi; }
note() { echo; echo "== $* =="; }

# ---------- 檢查 ----------
do_check() {
    note "驗收檢查"
    if uname -v | grep -q "PREEMPT_RT"; then echo "  ✓ PREEMPT_RT 內核: $(uname -r)"
    else echo "  ✗ 非 PREEMPT_RT 內核（sudo pro enable realtime-kernel）"; fi

    iso=$(cat /sys/devices/system/cpu/isolated 2>/dev/null)
    if [ "$iso" = "${IRQ_CORE}-${CYC_CORE}" ] || [ "$iso" = "${IRQ_CORE},${CYC_CORE}" ]; then
        echo "  ✓ isolcpus 生效: $iso"
    else echo "  ✗ isolcpus 未生效（目前:'${iso:-無}'）→ 先 --grub 後重開機"; fi

    gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)
    [ "${gov:-}" = performance ] && echo "  ✓ governor=performance" \
                                 || echo "  ✗ governor=${gov:-?}（跑 --apply）"

    echo "  基線量測（分別跑,門檻見規劃 §3.2）："
    echo "    cyclictest -m -Sp90 -i 1000 -h 100 -D 12h   # 隔離核 max < 50 µs"
    echo "    hwlatdetect --duration=2h                    # SMI ≈ 0(>10µs 換機/調 BIOS)"
}

if [ "$DO_CHECK" = 1 ]; then do_check; exit 0; fi
[ "$(id -u)" = 0 ] || [ "$APPLY" = 0 ] || { echo "需要 root（sudo）"; exit 1; }

# ---------- grub 內核參數 ----------
if [ "$DO_GRUB" = 1 ]; then
    note "grub 參數（寫入 /etc/default/grub.d/90-rt.cfg,重開機生效）"
    CMDLINE="isolcpus=${IRQ_CORE},${CYC_CORE} nohz_full=${IRQ_CORE},${CYC_CORE} rcu_nocbs=${IRQ_CORE},${CYC_CORE} irqaffinity=${HOUSE} skew_tick=1 intel_pstate=disable processor.max_cstate=1 intel_idle.max_cstate=0 nosoftlockup"
    echo "  $CMDLINE"
    run "printf 'GRUB_CMDLINE_LINUX_DEFAULT=\"\$GRUB_CMDLINE_LINUX_DEFAULT %s\"\n' '$CMDLINE' > /etc/default/grub.d/90-rt.cfg"
    run "update-grub"
    echo "  （重開機後用 --check 驗證;BIOS 另需關 SMT/Turbo/C-states,見規劃 §2.1）"
fi

# ---------- 執行期調校 ----------
note "CPU governor / turbo"
for c in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    run "echo performance > $c"
done
[ -f /sys/devices/system/cpu/intel_pstate/no_turbo ] && \
    run "echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo"
[ -f /sys/devices/system/cpu/cpufreq/boost ] && \
    run "echo 0 > /sys/devices/system/cpu/cpufreq/boost"

note "IRQ 綁定（其餘 IRQ → 家務核 ${HOUSE};專用介面 → 核 ${IRQ_CORE}）"
run "echo ${HOUSE} > /proc/irq/default_smp_affinity_list 2>/dev/null || true"
bind_irqs() { # <ifname>
    local ifc=$1
    for irq in $(grep -iE "${ifc}" /proc/interrupts | awk -F: '{gsub(/ /,"",$1); print $1}'); do
        run "echo ${IRQ_CORE} > /proc/irq/${irq}/smp_affinity_list"
    done
    # PREEMPT_RT 的 irq thread 提優先權（要搶在 cyclic(80) 之前收包）
    for tid in $(pgrep -f "irq/.*-${ifc}"); do
        run "chrt -f -p 85 ${tid}"
    done
}
[ -n "$NIC" ]   && bind_irqs "$NIC"
[ -n "$CANIF" ] && bind_irqs "$CANIF"

note "cpu_dma_latency（擋 C-state 深睡）"
if [ "$HOLD" = 1 ]; then
    echo "  常駐持有 /dev/cpu_dma_latency=0（Ctrl-C 釋放;建議做成 systemd service）"
    exec 9<> /dev/cpu_dma_latency || exit 1
    printf '\0\0\0\0' >&9
    echo "  已持有。按 Ctrl-C 結束。"
    while :; do sleep 3600; done
else
    echo "  略過（需常駐 holder 才有效 → 用 --hold-dma 或 systemd 化）"
fi

do_check
