from .controller import Controller


def main():
    controller = Controller()
    print("SIMULATION ONLY — arbitrary units, no hardware connected")
    controller.start(0)
    controller.set_targets(30, 10, 0)
    for second in range(1, 5):
        controller.heartbeat(second)
        print(second, controller.snapshot())
    print("Connection lost:", controller.tick(6))
    controller.heartbeat(7)
    print("Reconnect does not restart:", controller.snapshot())
    controller.reset()
    print("Explicit reset leaves idle:", controller.snapshot())


if __name__ == "__main__":
    main()
