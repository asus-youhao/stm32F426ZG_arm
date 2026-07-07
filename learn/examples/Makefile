# 一鍵建置/執行 learn/examples 全部 PC 可編譯範例
C_DIRS := l1/01-blink l1/02-uart-printf l1/03-systick-timeout l1/04-tim-tick-500hz \
          l2/01-can-raw l2/02-nmt-heartbeat l2/03-sdo-client l2/04-pdo-csp l2/05-bus-budget \
          l3/01-cia402-sm l3/02-units l3/03-ik-2link l3/04-watchdog-fbfresh \
          l3/05-ecat-skeleton l3/06-rt-1khz \
          project-a/wp2-single-axis project-a/wp3-joint-space project-a/wp4-task-space \
          project-a/wp5-dual-bus project-a/wp6-safety project-a/wp7-host-if \
          project-b/m5-csp-1khz project-b/m6-jitter-report

# 零依賴、可無人值守跑完的
RUN_DIRS := l1/01-blink l1/02-uart-printf l1/03-systick-timeout l1/04-tim-tick-500hz \
            l2/05-bus-budget l3/01-cia402-sm l3/02-units l3/03-ik-2link \
            l3/04-watchdog-fbfresh l3/05-ecat-skeleton \
            project-a/wp3-joint-space project-a/wp4-task-space project-b/m5-csp-1khz

all:
	@for d in $(C_DIRS); do $(MAKE) -s -C $$d || exit 1; done
	@echo "== all built =="

run: all
	@for d in $(RUN_DIRS); do echo; echo "===== $$d ====="; $(MAKE) -s -C $$d run || exit 1; done

cansim:
	@l2/06-run-all/run.sh
	@project-a/wp6-safety/run.sh
	@project-a/wp5-dual-bus/run.sh

clean:
	@for d in $(C_DIRS); do $(MAKE) -s -C $$d clean; done

.PHONY: all run cansim clean
