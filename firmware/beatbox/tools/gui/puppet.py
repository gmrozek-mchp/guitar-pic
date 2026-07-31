"""
Audio Envelope + Spectral Flux — Visual Inspection
dsPIC sends envelope (peak amplitude) + spectral flux per frame at ~23 Hz.
Two scrolling graphs aligned in time to visually correlate audio with transients.

Usage: python puppet.py [COM_PORT]
  ESC = quit
"""

import sys
import time
import threading
import serial
import serial.tools.list_ports
import pygame
from collections import deque

SERIAL_BAUD = 115200
WINDOW_WIDTH = 1500
WINDOW_HEIGHT = 850
FPS = 60
HISTORY_LEN = 500  # ~21 seconds at 23 Hz


class SerialReader(threading.Thread):
    def __init__(self, port, baud):
        super().__init__(daemon=True)
        self.port = port
        self.baud = baud
        self.running = True
        self.connected = False
        self.ser = None
        self.envelope_history = deque(maxlen=HISTORY_LEN)
        self.flux_history = deque(maxlen=HISTORY_LEN)
        self.bass_flux_history = deque(maxlen=HISTORY_LEN)
        self.last_env = 0
        self.last_flux = 0
        self.last_bass_flux = 0
        self.last_beat = 0
        self.last_phase = 0
        self.last_frame_time = 0  # frame period in 10us units
        self.bass_mode = False
        self.dspic_bpm = 0
        self.spectrum = [0] * 64
        self.beat_events = deque(maxlen=32)
        self.bass_beat_markers = deque(maxlen=HISTORY_LEN)   # 0/1/2 per sample
        self.full_beat_markers = deque(maxlen=HISTORY_LEN)   # 0/1/2 per sample
        self.band_select = 0  # 0=bass, 1=mid+high, 2=auto
        self.bass_peak_bin = 0
        self.full_peak_bin = 0
        self.beat_log = deque(maxlen=200)  # scrollable log of beat events
        self.paused = False

    def send_command(self, cmd):
        if self.ser:
            try:
                self.ser.write((cmd + '\n').encode('ascii'))
            except serial.SerialException:
                pass

    def run(self):
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.05)
            self.connected = True
            buffer = ""
            while self.running:
                data = self.ser.read(256)
                if data:
                    buffer += data.decode('ascii', errors='ignore')
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if line.startswith('D,'):
                            try:
                                parts = line[2:].split(',')
                                self.last_env = int(parts[0])
                                self.last_flux = int(parts[1])
                                bass_beat = int(parts[2]) if len(parts) > 2 else 0
                                full_beat = int(parts[3]) if len(parts) > 3 else 0
                                self.last_phase = int(parts[4]) if len(parts) > 4 else 0
                                self.last_frame_time = int(parts[5]) if len(parts) > 5 else 0
                                self.dspic_bpm = int(parts[6]) if len(parts) > 6 else 0
                                self.last_bass_flux = int(parts[7]) if len(parts) > 7 else 0
                                self.bass_peak_bin = int(parts[8]) if len(parts) > 8 else 0
                                self.full_peak_bin = int(parts[9]) if len(parts) > 9 else 0
                                if not self.paused:
                                    self.envelope_history.append(self.last_env)
                                    self.flux_history.append(self.last_flux)
                                    self.bass_flux_history.append(self.last_bass_flux)
                                    self.bass_beat_markers.append(bass_beat)
                                    self.full_beat_markers.append(full_beat)
                                # Log beat events
                                t = time.strftime("%H:%M:%S")
                                if bass_beat > 0:
                                    hz = self.bass_peak_bin * 23.4375
                                    strength = "BIG" if bass_beat == 2 else "   "
                                    self.beat_log.append(
                                        f"{t} BASS  {strength} bin {self.bass_peak_bin:>3d} ({hz:>6.0f} Hz)")
                                if full_beat > 0:
                                    hz = self.full_peak_bin * 23.4375
                                    strength = "BIG" if full_beat == 2 else "   "
                                    self.beat_log.append(
                                        f"{t} FULL  {strength} bin {self.full_peak_bin:>3d} ({hz:>6.0f} Hz)")
                                # Fire GUI flash from selected band
                                if self.band_select == 0:
                                    selected_beat = bass_beat
                                elif self.band_select == 1:
                                    selected_beat = full_beat
                                else:
                                    selected_beat = max(bass_beat, full_beat)
                                if selected_beat > 0:
                                    self.beat_events.append(selected_beat)
                            except (ValueError, IndexError):
                                pass
                        elif line.startswith('S,'):
                            try:
                                parts = line[2:].split(',')
                                if len(parts) >= 64:
                                    self.spectrum = [min(255, int(p)) for p in parts[:64]]
                            except (ValueError, IndexError):
                                pass
        except serial.SerialException as e:
            print(f"Serial error: {e}")
            self.connected = False

    def stop(self):
        self.running = False


class Slider:
    def __init__(self, x, y, width, min_val, max_val, initial, label):
        self.rect = pygame.Rect(x, y, width, 30)
        self.min_val, self.max_val = min_val, max_val
        self.value = initial
        self.label = label
        self.dragging = False

    def handle_event(self, event):
        if event.type == pygame.MOUSEBUTTONDOWN and self.rect.collidepoint(event.pos):
            self.dragging = True
            return self._update(event.pos[0])
        elif event.type == pygame.MOUSEBUTTONUP and self.dragging:
            self.dragging = False
            return True
        elif event.type == pygame.MOUSEMOTION and self.dragging:
            return self._update(event.pos[0])
        return False

    def _update(self, mx):
        r = max(0.0, min(1.0, (mx - self.rect.x) / self.rect.width))
        self.value = self.min_val + r * (self.max_val - self.min_val)
        return True

    def draw(self, surface, font):
        ty = self.rect.y + 15
        pygame.draw.line(surface, (80, 80, 100), (self.rect.x, ty),
                         (self.rect.x + self.rect.width, ty), 3)
        r = (self.value - self.min_val) / (self.max_val - self.min_val)
        kx = int(self.rect.x + r * self.rect.width)
        pygame.draw.circle(surface, (220, 220, 240), (kx, ty), 8)
        surface.blit(font.render(f"{self.label}: {int(self.value)}", True, (180, 200, 220)),
                     (self.rect.x, self.rect.y - 16))


class HeadBobber:
    """Simple down-and-back nod. Two positions only: neutral and head-down."""
    T_DOWN = 0.12
    T_RETURN = 0.25
    T_TOTAL = T_DOWN + T_RETURN

    def __init__(self):
        self.active = False
        self.timer = 0.0
        self.depth = 40.0

    def trigger(self):
        if not self.active:
            self.active = True
            self.timer = 0.0

    def update(self, dt):
        if self.active:
            self.timer += dt
            if self.timer >= self.T_TOTAL:
                self.active = False

    def get_offset(self):
        if not self.active:
            return 0.0
        t = self.timer
        if t < self.T_DOWN:
            p = t / self.T_DOWN
            return self.depth * (p * p)
        else:
            p = (t - self.T_DOWN) / self.T_RETURN
            return self.depth * (1.0 - p) * (1.0 - p)


def draw_beavis(surface, cx, cy, head_offset):
    SKIN, HAIR, SHIRT = (255, 220, 170), (255, 230, 100), (40, 40, 45)
    pygame.draw.polygon(surface, SHIRT, [(cx-30, cy), (cx+30, cy), (cx+25, cy+80), (cx-25, cy+80)])
    head_y = cy - 20 + int(head_offset * 0.6)
    pygame.draw.line(surface, SKIN, (cx, cy), (cx, head_y + 20), 6)
    pygame.draw.ellipse(surface, SKIN, (cx-24, head_y-30, 48, 60))
    hair_y = head_y - 28 + int(head_offset * 0.5)
    for dx, h in [(-15, -22), (-3, -30), (8, -25), (18, -18)]:
        pygame.draw.polygon(surface, HAIR, [(cx+dx-5, hair_y), (cx+dx+2, hair_y+h), (cx+dx+8, hair_y)])
    ey = head_y - 5
    pygame.draw.ellipse(surface, (240, 240, 240), (cx-16, ey-5, 13, 11))
    pygame.draw.ellipse(surface, (240, 240, 240), (cx+3, ey-5, 13, 11))
    pygame.draw.circle(surface, (20, 20, 30), (cx-10, ey+1), 3)
    pygame.draw.circle(surface, (20, 20, 30), (cx+10, ey+1), 3)
    mo = min(abs(head_offset) / 15.0, 1.0)
    mh = int(5 + mo * 12)
    pygame.draw.ellipse(surface, (160, 50, 50), (cx-10, head_y+12, 20, mh))
    if mh > 6:
        for i in range(3):
            pygame.draw.rect(surface, (230, 230, 220), (cx-8+i*6, head_y+13, 5, min(5, mh//2)))


def draw_graph(surface, x, y, w, h, data, color, fill_color, title, font,
               show_y_axis=False, show_points=False, max_val=1000,
               zoom=1.0, pan=0.0):
    """Draw a scrolling time-series graph with optional zoom/pan."""
    pygame.draw.rect(surface, (30, 30, 42), (x, y, w, h), border_radius=4)

    # Title
    surface.blit(font.render(title, True, color), (x + 10, y + 5))

    if len(data) < 2:
        return

    y_axis_w = 40 if show_y_axis else 0
    graph_left = x + 5 + y_axis_w
    graph_right = x + w - 5
    graph_top = y + 25
    graph_bottom = y + h - 10
    gw = graph_right - graph_left
    gh = graph_bottom - graph_top

    # Y-axis scale
    if show_y_axis:
        tick_color = (120, 120, 120)
        num_ticks = 5
        for t in range(num_ticks + 1):
            tick_val = int(max_val * t / num_ticks)
            ty = graph_bottom - int(t * gh / num_ticks)
            label = font.render(str(tick_val), True, tick_color)
            surface.blit(label, (x + 5, ty - 6))
            pygame.draw.line(surface, (50, 50, 60), (graph_left, ty), (graph_right, ty), 1)

    hist = list(data)
    n = len(hist)

    # Compute visible window based on zoom/pan
    visible_count = max(2, int(n / zoom))
    max_offset = n - visible_count
    start_idx = int(max_offset * (1.0 - pan))
    end_idx = start_idx + visible_count
    visible_hist = hist[start_idx:end_idx]

    # Build points for visible window
    points = []
    for i, val in enumerate(visible_hist):
        px = graph_left + int(i * gw / len(visible_hist))
        norm = min(val / float(max_val), 1.0)
        py = graph_bottom - int(norm * gh)
        points.append((px, py))

    if len(points) > 1:
        # Fill under curve
        fill_pts = points + [(points[-1][0], graph_bottom), (points[0][0], graph_bottom)]
        fill_surf = pygame.Surface((w, h), pygame.SRCALPHA)
        adjusted = [(px - x, py - y) for px, py in fill_pts]
        pygame.draw.polygon(fill_surf, (*fill_color, 50), adjusted)
        surface.blit(fill_surf, (x, y))

        # Line
        pygame.draw.lines(surface, color, False, points, 2)

        # Individual sample points
        if show_points:
            for px, py in points:
                pygame.draw.circle(surface, color, (px, py), 3)

    # Current value + zoom indicator
    val_text = font.render(f"{visible_hist[-1] if visible_hist else 0}", True, (200, 200, 200))
    surface.blit(val_text, (x + w - 50, y + 5))
    if zoom > 1.05:
        zoom_text = font.render(f"{zoom:.1f}x [R=reset]", True, (180, 180, 80))
        surface.blit(zoom_text, (x + w - 150, y + h - 18))

    return start_idx, end_idx, visible_count


def find_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if any(k in p.description.upper() for k in ['USB', 'SERIAL', 'UART']):
            return p.device
    return ports[0].device if ports else None


def select_port_screen():
    pygame.init()
    screen = pygame.display.set_mode((500, 400))
    pygame.display.set_caption("Select COM Port")
    clock = pygame.time.Clock()
    font = pygame.font.SysFont("Consolas", 16)
    font_big = pygame.font.SysFont("Consolas", 22)

    def get_ports():
        return serial.tools.list_ports.comports()

    ports = get_ports()
    hover_idx = -1
    scroll_offset = 0

    while True:
        clock.tick(30)

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit()
                return None
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    pygame.quit()
                    return None
                elif event.key == pygame.K_F5:
                    ports = get_ports()
            elif event.type == pygame.MOUSEBUTTONDOWN:
                mx, my = event.pos
                if event.button == 1:  # left click
                    # Refresh button
                    if 180 <= mx <= 320 and 355 <= my <= 385:
                        ports = get_ports()
                    # Port list clicks
                    elif 20 <= mx <= 480 and 70 <= my <= 340:
                        idx = (my - 70 + scroll_offset) // 40
                        if 0 <= idx < len(ports):
                            pygame.quit()
                            return ports[idx].device
                elif event.button == 4:  # scroll up
                    scroll_offset = max(0, scroll_offset - 40)
                elif event.button == 5:  # scroll down
                    max_scroll = max(0, len(ports) * 40 - 270)
                    scroll_offset = min(max_scroll, scroll_offset + 40)

        # Hover detection
        mx, my = pygame.mouse.get_pos()
        if 20 <= mx <= 480 and 70 <= my <= 340:
            hover_idx = (my - 70 + scroll_offset) // 40
        else:
            hover_idx = -1

        # Draw
        screen.fill((20, 20, 30))
        screen.blit(font_big.render("Select COM Port", True, (220, 220, 240)), (160, 20))
        screen.blit(font.render("Click a port to connect  |  F5 = Refresh  |  ESC = Quit",
                                True, (100, 100, 120)), (40, 48))

        # Port list area
        list_rect = pygame.Rect(20, 70, 460, 270)
        pygame.draw.rect(screen, (30, 30, 42), list_rect, border_radius=4)

        if not ports:
            screen.blit(font.render("No COM ports detected.", True, (200, 100, 100)), (140, 180))
        else:
            clip = screen.subsurface(list_rect)
            for i, p in enumerate(ports):
                row_y = i * 40 - scroll_offset
                if row_y < -40 or row_y > 270:
                    continue
                row_rect = pygame.Rect(0, row_y, 460, 38)
                if i == hover_idx:
                    pygame.draw.rect(clip, (50, 60, 80), row_rect, border_radius=3)
                port_text = f"{p.device}"
                desc_text = f"{p.description}" if p.description != p.device else ""
                clip.blit(font.render(port_text, True, (180, 220, 255)), (10, row_y + 4))
                if desc_text:
                    clip.blit(font.render(desc_text, True, (120, 140, 160)), (10, row_y + 21))

        # Refresh button
        btn_rect = pygame.Rect(180, 355, 140, 30)
        btn_hover = btn_rect.collidepoint(mx, my)
        btn_color = (60, 80, 100) if btn_hover else (40, 50, 65)
        pygame.draw.rect(screen, btn_color, btn_rect, border_radius=4)
        pygame.draw.rect(screen, (80, 100, 120), btn_rect, 1, border_radius=4)
        screen.blit(font.render("Refresh", True, (200, 220, 240)), (220, 361))

        pygame.display.flip()


def main():
    if len(sys.argv) > 1:
        port = sys.argv[1]
    else:
        port = select_port_screen()
        if not port:
            sys.exit(0)
    print(f"Using: {port}")

    reader = SerialReader(port, SERIAL_BAUD)
    reader.start()

    pygame.init()
    screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
    pygame.display.set_caption("Audio Envelope + Spectral Flux")
    clock = pygame.time.Clock()
    font = pygame.font.SysFont("Consolas", 15)
    font_big = pygame.font.SysFont("Consolas", 18)

    # Beat sync from firmware (LED and GUI fire together)
    beat_flash = 0.0
    beat_type = 0  # 1=red, 2=yellow
    bobber = HeadBobber()

    # Band selection: 0=bass, 1=mid+high, 2=auto
    band_select = 0
    band_names = ["BASS", "MID+HIGH", "AUTO"]
    reader.send_command("F,0")


    # Zoom/pan state for spectral flux graph
    flux_zoom = 1.0        # 1.0 = show all 500 samples, higher = zoomed in
    flux_pan = 0.0         # 0.0 = rightmost (latest), 1.0 = leftmost (oldest)
    flux_dragging = False
    flux_drag_start_x = 0
    flux_drag_start_pan = 0.0

    running = True
    while running:
        clock.tick(FPS)

        # Compute layout for mouse hit-testing
        margin = 30
        spec_h = 100
        graph_h = 120
        gap = 8
        spec_y = 45
        spec_w = WINDOW_WIDTH - 500
        env_y = spec_y + spec_h + gap + 15
        bass_flux_y = env_y + graph_h + gap
        full_flux_y = bass_flux_y + graph_h + gap
        # Both flux graphs respond to zoom/pan
        bass_flux_rect = pygame.Rect(margin, bass_flux_y, spec_w, graph_h)
        full_flux_rect = pygame.Rect(margin, full_flux_y, spec_w, graph_h)

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
                elif event.key == pygame.K_SPACE:
                    bobber.trigger()
                    beat_flash = 1.0
                elif event.key == pygame.K_r:
                    flux_zoom = 1.0
                    flux_pan = 0.0
                elif event.key == pygame.K_p:
                    reader.paused = not reader.paused
                elif event.key == pygame.K_1:
                    band_select = 0
                    reader.band_select = 0
                    reader.send_command("F,0")
                elif event.key == pygame.K_2:
                    band_select = 1
                    reader.band_select = 1
                    reader.send_command("F,1")
                elif event.key == pygame.K_3:
                    band_select = 2
                    reader.band_select = 2
                    reader.send_command("F,2")
            elif event.type == pygame.MOUSEBUTTONDOWN:
                if bass_flux_rect.collidepoint(event.pos) or full_flux_rect.collidepoint(event.pos):
                    if event.button == 4:  # scroll up = zoom in
                        flux_zoom = min(flux_zoom * 1.3, 50.0)
                    elif event.button == 5:  # scroll down = zoom out
                        flux_zoom = max(flux_zoom / 1.3, 1.0)
                        if flux_zoom == 1.0:
                            flux_pan = 0.0
                    elif event.button == 1:  # left click = start drag
                        flux_dragging = True
                        flux_drag_start_x = event.pos[0]
                        flux_drag_start_pan = flux_pan
            elif event.type == pygame.MOUSEBUTTONUP:
                if event.button == 1:
                    flux_dragging = False
            elif event.type == pygame.MOUSEMOTION:
                if flux_dragging:
                    dx = event.pos[0] - flux_drag_start_x
                    pan_delta = dx / (spec_w * flux_zoom) * 2.0
                    flux_pan = max(0.0, min(1.0, flux_drag_start_pan + pan_delta))

        dt = clock.get_time() / 1000.0

        # Process beats from firmware
        while reader.beat_events:
            bt = reader.beat_events.popleft()
            beat_flash = 1.0
            beat_type = bt

        # Reset beat_type only after flash fades
        if beat_flash <= 0:
            beat_type = 0

        beat_flash = max(0.0, beat_flash - dt * 4.0)

        # Head position from dsPIC phase (0=up, 1000=down)
        head_offset = (reader.last_phase / 1000.0) * 40.0  # 0-40px

        # Flash background on beat
        if beat_flash > 0:
            b = int(beat_flash * 80)
            if beat_type == 2:
                bg = (20 + b, 20 + b, 30)       # yellow flash (big beat)
            else:
                bg = (20 + b, 20 + b // 4, 30)  # red flash (normal beat)
        else:
            bg = (20, 20, 30)
        screen.fill(bg)

        # Beavis (right side) — driven by dsPIC phase
        draw_beavis(screen, WINDOW_WIDTH - 90, 200, head_offset)

        # === TOP: Spectrum bars ===
        pygame.draw.rect(screen, (30, 30, 42), (margin, spec_y, spec_w, spec_h), border_radius=4)
        screen.blit(font.render("FFT SPECTRUM (64 bins, 0-1.5 kHz)", True, (140, 140, 160)),
                    (margin + 10, spec_y + 3))

        spectrum = reader.spectrum
        bar_spacing = spec_w / 64
        bar_w = max(2, int(bar_spacing) - 1)
        for i in range(64):
            val = spectrum[i]
            bh = int((val / 255.0) * (spec_h - 25))
            if bh < 1: bh = 1
            bx = margin + int(i * bar_spacing)
            by = spec_y + spec_h - bh - 2
            pygame.draw.rect(screen, (100, 180, 255), (bx, by, bar_w, bh))

        # X-axis labels
        for i in range(0, 64, 8):
            lx = margin + int(i * bar_spacing)
            hz = (i + 1) * 23.4375
            label = f"{hz:.0f}"
            screen.blit(font.render(label, True, (80, 80, 100)), (lx, spec_y + spec_h + 1))

        # === MIDDLE: Audio Envelope ===
        env_y = spec_y + spec_h + gap + 15
        draw_graph(screen, margin, env_y, spec_w, graph_h,
                   reader.envelope_history,
                   (100, 200, 255), (60, 150, 220),
                   "AUDIO ENVELOPE (peak amplitude per frame)", font)

        # === BASS FLUX (bins 1-10, ~23-234 Hz) ===
        bass_flux_y = env_y + graph_h + gap
        bass_title = "BASS FLUX (bins 1-10, 23-234 Hz)"
        if reader.bass_peak_bin > 0:
            bass_hz = reader.bass_peak_bin * 23.4375
            bass_title += f"  Peak: bin {reader.bass_peak_bin} ({bass_hz:.0f} Hz)"
        bass_view = draw_graph(screen, margin, bass_flux_y, spec_w, graph_h,
                               reader.bass_flux_history,
                               (255, 160, 60), (200, 120, 40),
                               bass_title, font,
                               show_y_axis=True, show_points=True,
                               zoom=flux_zoom, pan=flux_pan)

        # Beat markers on bass graph
        bass_markers = list(reader.bass_beat_markers)
        if bass_view and len(bass_markers) > 1:
            flux_graph_left = margin + 5 + 40
            flux_graph_right = margin + spec_w - 5
            fgw = flux_graph_right - flux_graph_left
            start_idx, end_idx, visible_count = bass_view
            for i in range(start_idx, min(end_idx, len(bass_markers))):
                m = bass_markers[i]
                if m > 0:
                    local_i = i - start_idx
                    mx = flux_graph_left + int(local_i * fgw / visible_count)
                    mc = (255, 220, 40) if m == 2 else (255, 80, 80)
                    pygame.draw.line(screen, mc, (mx, bass_flux_y + 25), (mx, bass_flux_y + graph_h - 10), 2)

        # === FULL FLUX (bins 11-255, ~257-6000 Hz) ===
        full_flux_y = bass_flux_y + graph_h + gap
        full_title = "MID+HIGH FLUX (bins 11-255, 257-6000 Hz)"
        if reader.full_peak_bin > 0:
            full_hz = reader.full_peak_bin * 23.4375
            full_title += f"  Peak: bin {reader.full_peak_bin} ({full_hz:.0f} Hz)"
        full_view = draw_graph(screen, margin, full_flux_y, spec_w, graph_h,
                               reader.flux_history,
                               (80, 255, 100), (50, 200, 70),
                               full_title, font,
                               show_y_axis=True, show_points=True,
                               zoom=flux_zoom, pan=flux_pan)

        # Beat markers on full graph
        full_markers = list(reader.full_beat_markers)
        if full_view and len(full_markers) > 1:
            flux_graph_left = margin + 5 + 40
            flux_graph_right = margin + spec_w - 5
            fgw = flux_graph_right - flux_graph_left
            start_idx, end_idx, visible_count = full_view
            for i in range(start_idx, min(end_idx, len(full_markers))):
                m = full_markers[i]
                if m > 0:
                    local_i = i - start_idx
                    mx = flux_graph_left + int(local_i * fgw / visible_count)
                    mc = (255, 220, 40) if m == 2 else (255, 80, 80)
                    pygame.draw.line(screen, mc, (mx, full_flux_y + 25), (mx, full_flux_y + graph_h - 10), 2)

        # Paused indicator
        if reader.paused:
            pause_surf = font_big.render("PAUSED [P]", True, (255, 80, 80))
            screen.blit(pause_surf, (margin + spec_w // 2 - 50, bass_flux_y + 5))

        # Status + BPM + Frame Time
        status = "CONNECTED" if reader.connected else "WAITING..."
        sc = (80, 220, 80) if reader.connected else (220, 140, 60)
        screen.blit(font_big.render(f"{port} [{status}]", True, sc), (30, 10))
        if reader.dspic_bpm > 0:
            bpm_color = (255, 220, 80) if beat_type == 2 else (200, 255, 150)
            screen.blit(font_big.render(f"{reader.dspic_bpm} BPM", True, bpm_color),
                        (30, 30))
        # Band selection indicator
        band_colors = [(255, 160, 60), (80, 255, 100), (180, 180, 80)]
        band_label = f"Beat Band: {band_names[band_select]} [1/2/3]"
        screen.blit(font.render(band_label, True, band_colors[band_select]), (300, 33))
        # Frame timing from dsPIC (in 10us units → ms)
        ft_ms = reader.last_frame_time / 100.0
        ft_color = (80, 200, 80) if 40.0 < ft_ms < 46.0 else (255, 100, 100)
        screen.blit(font.render(f"Frame: {ft_ms:.1f}ms", True, ft_color), (200, 35))
        screen.blit(font.render("ESC=quit SPACE=test P=pause R=reset 1/2/3=band", True, (70, 70, 90)),
                    (WINDOW_WIDTH - 380, 10))

        # Slider

        # Beat flash circle (top right) — red or yellow
        circle_x, circle_y = spec_w + margin - 30, 80
        if beat_flash > 0:
            r = int(15 + beat_flash * 20)
            s = pygame.Surface((r*2, r*2), pygame.SRCALPHA)
            if beat_type == 2:
                pygame.draw.circle(s, (255, 220, 40, int(beat_flash * 220)), (r, r), r)
            else:
                pygame.draw.circle(s, (255, 80, 80, int(beat_flash * 220)), (r, r), r)
            screen.blit(s, (circle_x - r, circle_y - r))
        pygame.draw.circle(screen, (80, 40, 40), (circle_x, circle_y), 17, 2)

        # Frame count
        n = len(reader.flux_history)
        screen.blit(font.render(f"Frames: {n}", True, (80, 80, 100)), (WINDOW_WIDTH - 120, 10))

        # === BEAT LOG PANEL (right side) ===
        log_x = spec_w + margin + 15
        log_y = spec_y
        log_w = WINDOW_WIDTH - log_x - 10
        log_h = WINDOW_HEIGHT - log_y - 10
        pygame.draw.rect(screen, (25, 25, 35), (log_x, log_y, log_w, log_h), border_radius=4)
        screen.blit(font.render("BEAT LOG", True, (160, 160, 180)), (log_x + 5, log_y + 3))

        log_entries = list(reader.beat_log)
        line_h = 14
        max_lines = (log_h - 25) // line_h
        visible_log = log_entries[-max_lines:]
        for idx, entry in enumerate(visible_log):
            ey = log_y + 20 + idx * line_h
            if "BASS" in entry:
                ec = (255, 160, 60)
            else:
                ec = (80, 255, 100)
            if "BIG" in entry:
                ec = (255, 220, 40)
            screen.blit(font.render(entry, True, ec), (log_x + 5, ey))

        pygame.display.flip()

    reader.stop()
    pygame.quit()


if __name__ == "__main__":
    main()
