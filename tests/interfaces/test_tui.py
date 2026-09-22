import pytest
from textual.widgets import Button, Static

from srcds64_ui.model import Settings
from srcds64_ui.tui import ConfirmInstall, ManagerApp


async def wait_for_job(pilot, app):
    for _ in range(100):
        await pilot.pause(0.02)
        if app.job is None:
            return
    pytest.fail("UI did not finish job")


async def test_preview_and_failed_verification_keep_ui_usable(fake_backend):
    app = ManagerApp(Settings())
    async with app.run_test(size=(120, 40)) as pilot:
        await pilot.click("#plan")
        await wait_for_job(pilot, app)
        assert "Completed successfully" in str(app.query_one("#status", Static).render())
        await pilot.click("#verify")
        await wait_for_job(pilot, app)
        assert "exit 3" in str(app.query_one("#status", Static).render())
        assert not app.query_one("#install", Button).disabled


async def test_install_requires_confirmation_and_back_does_nothing(fake_backend):
    app = ManagerApp(Settings())
    async with app.run_test(size=(100, 35)) as pilot:
        await pilot.click("#install")
        assert isinstance(app.screen, ConfirmInstall)
        assert app.job is None
        await pilot.click("#back")
        assert app.job is None
        await pilot.click("#install")
        await pilot.click("#confirm")
        await wait_for_job(pilot, app)
        assert "Completed successfully" in str(app.query_one("#status", Static).render())


async def test_small_terminal_renders():
    app = ManagerApp(Settings())
    async with app.run_test(size=(80, 24)) as pilot:
        await pilot.pause()
        assert app.query_one("#plan", Button).region.bottom <= 24
        assert app.query_one("#verify", Button).region.right <= 80
