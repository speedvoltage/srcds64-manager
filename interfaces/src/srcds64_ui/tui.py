"""Keyboard-driven terminal UI; works in Linux terminals and Windows Terminal."""
from __future__ import annotations

from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical, VerticalScroll
from textual.screen import ModalScreen
from textual.widgets import Button, Checkbox, Footer, Header, Input, Label, Log, Select, Static

from .jobs import Job
from .model import GAMES, NOTICE, Settings, load_settings, save_settings
from .presentation import FIELDS, confirmation_text, plain_output


class ConfirmInstall(ModalScreen[bool]):
    CSS = """
    ConfirmInstall { align: center middle; }
    #confirmation { width: 68; height: auto; padding: 2; border: thick $accent; background: $surface; }
    #confirmation Horizontal { height: 3; margin-top: 1; }
    """

    def __init__(self, settings: Settings):
        super().__init__()
        self.confirmation = confirmation_text(settings)

    def compose(self) -> ComposeResult:
        with Vertical(id="confirmation"):
            yield Static(self.confirmation, markup=False)
            with Horizontal():
                yield Button("Back", id="back", variant="default")
                yield Button("Install server", id="confirm", variant="warning")

    def on_mount(self) -> None:
        self.query_one("#back", Button).focus()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        self.dismiss(event.button.id == "confirm")


class ManagerApp(App):
    TITLE = "SRCDS 64 Manager"
    SUB_TITLE = "Server installer"
    BINDINGS = [("ctrl+q", "quit", "Quit"), ("ctrl+p", "preview", "Preview"),
                ("ctrl+s", "save", "Save settings")]
    CSS = """
    Screen { background: $background; }
    #notice { height: auto; padding: 1 2; color: $text-muted; }
    #body { height: 1fr; }
    #settings { width: 48; padding: 0 2; border-right: solid $primary; }
    #settings Label { margin-top: 1; }
    #settings Input, #settings Select { width: 100%; }
    #output-pane { width: 1fr; padding: 0 2; }
    #status { height: auto; padding: 1 0; color: $accent; }
    #output { height: 1fr; border: round $primary; }
    #actions { height: auto; padding: 1; layout: horizontal; }
    #actions Button { min-width: 12; margin-right: 1; }
    """

    def __init__(self, settings: Settings | None = None):
        super().__init__()
        self.startup_error = ""
        try:
            self.settings = settings or load_settings()
        except ValueError as error:
            self.settings = Settings()
            self.startup_error = str(error)
        self.job: Job | None = None

    def compose(self) -> ComposeResult:
        yield Header()
        yield Static(NOTICE, id="notice")
        with Horizontal(id="body"):
            with VerticalScroll(id="settings"):
                yield Label("GAME")
                yield Select([(g["name"], g["id"]) for g in GAMES],
                             value=self.settings.game if self.settings.game in {g['id'] for g in GAMES} else 'hl2dm',
                             allow_blank=False, id="game")
                for name, label, placeholder in FIELDS:
                    yield Label(label)
                    yield Input(getattr(self.settings, name), placeholder=placeholder, id=name)
                yield Checkbox("Validate game downloads", value=self.settings.validate, id="validate")
            with Vertical(id="output-pane"):
                yield Static("Ready • Preview an installation to begin", id="status")
                yield Log(id="output", highlight=False, max_lines=10000)
        with Horizontal(id="actions"):
            yield Button("Preview", id="plan", variant="primary")
            yield Button("Set up tools", id="doctor")
            yield Button("Install", id="install", variant="warning")
            yield Button("Verify", id="verify")
            yield Button("Save", id="save")
        yield Footer()

    def on_mount(self) -> None:
        self.set_interval(0.05, self.poll_job)
        if self.startup_error:
            self.query_one("#output", Log).write(self.startup_error + "\nUsing defaults; Save replaces invalid settings.\n")

    def read_settings(self) -> Settings:
        return Settings(game=str(self.query_one("#game", Select).value),
                        validate=self.query_one("#validate", Checkbox).value,
                        **{name: self.query_one(f"#{name}", Input).value.strip() for name, _, _ in FIELDS})

    def action_save(self) -> None:
        if self.job:
            return
        try:
            settings = self.read_settings()
            settings.command("plan")
            save_settings(settings)
            self.notify("Settings saved")
        except (ValueError, OSError) as error:
            self.notify(str(error), severity="error", timeout=10)

    def action_preview(self) -> None:
        self.start_action("plan")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        action = event.button.id
        if action == "save":
            self.action_save()
        elif action == "install":
            if not self.job:
                try:
                    settings = self.read_settings()
                    settings.command("install")
                except ValueError as error:
                    self.notify(str(error), severity="error", timeout=10)
                    return
                self.push_screen(ConfirmInstall(settings), lambda approved: self.start_action("install") if approved else None)
        elif action in ("plan", "doctor", "verify"):
            self.start_action(action)

    def start_action(self, action: str) -> None:
        if self.job:
            return
        try:
            command = self.read_settings().command(action)
        except ValueError as error:
            self.notify(str(error), severity="error", timeout=10)
            return
        self.query_one("#output", Log).write(f"\n── {action.upper()} ──\n")
        self.query_one("#status", Static).update(f"Running: {action} • Keep this window open")
        self.set_busy(True)
        self.job = Job(command)
        self.job.start()

    def set_busy(self, busy: bool) -> None:
        for widget in self.query("Button, Input, Select, Checkbox"):
            widget.disabled = busy

    def poll_job(self) -> None:
        if not self.job:
            return
        for event in self.job.drain():
            if event.kind == "output":
                self.query_one("#output", Log).write(plain_output(event.text))
            elif event.kind == "finished":
                message = "Completed successfully" if event.code == 0 else f"Failed (exit {event.code}) • See output"
                self.query_one("#status", Static).update(message)
                self.job = None
                self.set_busy(False)

    def action_quit(self) -> None:
        if self.job:
            self.notify("An operation is running. Wait for it to finish before closing.", severity="warning")
        else:
            self.exit()


def main() -> None:
    ManagerApp().run()


if __name__ == "__main__":
    main()
