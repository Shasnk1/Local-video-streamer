#!/usr/bin/env python3
"""Simple movie player with control buttons and manual file selection.
   Built with tkinter. Plays video using platform default handler.
   Starts/stops the movie_server.exe server automatically.
"""
import os
import sys
import subprocess
import socket
import time
import tkinter as tk
from tkinter import filedialog, messagebox

class MoviePlayer:
    def __init__(self, root):
        self.root = root
        self.root.title("Movie Server Player")
        self.root.geometry("400x300")

        # Server management
        self.server_process = None
        self.server_exe_path = os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            "movie_server.exe"
        )
        self.is_server_running = False
        self.server_port = 8080
        self.server_host = "127.0.0.1"

        # Create widgets
        self.status_var = tk.StringVar(value="Status: Stopped")
        self.url_var = tk.StringVar(value="URL: http://127.0.0.1:8080")

        self.create_widgets()
        self.check_server_status_periodically()

    def create_widgets(self):
        main_frame = tk.Frame(self.root, padx=10, pady=10)
        main_frame.pack(fill=tk.BOTH, expand=True)

        # Status label
        tk.Label(main_frame, textvariable=self.status_var, font=("Arial", 12)).pack(pady=5)

        # URL display
        url_frame = tk.Frame(main_frame)
        url_frame.pack(fill=tk.X, pady=5)
        tk.Label(url_frame, text="URL:", width=6).pack(side=tk.LEFT)
        url_entry = tk.Entry(url_frame, textvariable=self.url_var, state="readonly")
        url_entry.pack(side=tk.LEFT, fill=tk.X, expand=True)

        # Buttons frame
        btn_frame = tk.Frame(main_frame)
        btn_frame.pack(pady=10)

        self.start_btn = tk.Button(btn_frame, text="Start Server",
                                 command=self.start_server, width=12)
        self.stop_btn = tk.Button(btn_frame, text="Stop Server",
                               state="disabled", width=12, command=self.stop_server)
        self.open_btn = tk.Button(btn_frame, text="Open Browser",
                               state="disabled", width=12, command=self.open_browser)
        self.add_movie_btn = tk.Button(btn_frame, text="Add Movie File",
                                    width=12, command=self.add_movie_file)

        self.start_btn.pack(side=tk.LEFT, padx=5)
        self.stop_btn.pack(side=tk.LEFT, padx=5)
        self.open_btn.pack(side=tk.LEFT, padx=5)
        self.add_movie_btn.pack(side=tk.LEFT, padx=5)

    def check_server_status(self):
        try:
            with socket.create_connection((self.server_host, self.server_port), timeout=1):
                return True
        except (socket.timeout, ConnectionRefusedError):
            return False

    def check_server_status_periodically(self):
        running = self.check_server_status()
        if running != self.is_server_running:
            self.is_server_running = running
            if running:
                self.status_var.set("Status: Running")
                self.start_btn.config(state="disabled")
                self.stop_btn.config(state="normal")
                self.open_btn.config(state="normal")
                self.add_movie_btn.config(state="normal")
            else:
                self.status_var.set("Status: Stopped")
                self.start_btn.config(state="normal")
                self.stop_btn.config(state="disabled")
                self.open_btn.config(state="disabled")
                self.add_movie_btn.config(state="disabled")
        self.root.after(2000, self.check_server_status_periodically)

    def start_server(self):
        if self.is_server_running:
            return

        if not os.path.exists(self.server_exe_path):
            messagebox.showerror("Error", f"Server executable not found:\n{self.server_exe_path}")
            return

        try:
            # Start server as a background process
            self.server_process = subprocess.Popen(
                [self.server_exe_path],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                cwd=os.path.dirname(self.server_exe_path)
            )

            # Wait for server to be ready (max 20 seconds)
            for _ in range(100):
                if self.check_server_status():
                    self.is_server_running = True
                    self.status_var.set("Status: Starting...")
                    self.root.update()

                    # Wait a moment then set to running
                    self.root.after(1000, lambda: self.status_var.set("Status: Running"))
                    self.start_btn.config(state="disabled")
                    self.stop_btn.config(state="normal")
                    self.open_btn.config(state="normal")
                    self.add_movie_btn.config(state="normal")
                    return
                time.sleep(0.2)

            messagebox.showwarning("Warning", "Server started but may not be responding properly")
            self.is_server_running = True
            self.status_var.set("Status: Starting...")

        except Exception as e:
            messagebox.showerror("Error", f"Failed to start server: {e}")

    def stop_server(self):
        if not self.is_server_running:
            return

        if self.server_process:
            self.server_process.terminate()
            try:
                self.server_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.server_process.kill()
            self.server_process = None

        self.is_server_running = False
        self.status_var.set("Status: Stopped")
        self.start_btn.config(state="normal")
        self.stop_btn.config(state="disabled")
        self.open_btn.config(state="disabled")
        self.add_movie_btn.config(state="disabled")

    def open_browser(self):
        import webbrowser
        webbrowser.open(f"http://{self.server_host}:{self.server_port}")

    def add_movie_file(self):
        file_path = filedialog.askopenfilename(
            title="Select Movie File",
            filetypes=[(".mp4", ".mp4"), (".mkv", ".mkv"), (".avi", ".avi"),
                      ("All video files", "*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.m4v")]
        )

        if file_path:
            # Copy to movies directory
            movies_dir = os.path.join(
                os.path.dirname(self.server_exe_path),
                "movies"
            )
            os.makedirs(movies_dir, exist_ok=True)

            dest_path = os.path.join(movies_dir, os.path.basename(file_path))
            try:
                with open(file_path, 'rb') as src, open(dest_path, 'wb') as dst:
                    dst.write(src.read())

                messagebox.showinfo("Success", f"Added movie file:\n{os.path.basename(dest_path)}")

            except Exception as e:
                messagebox.showerror("Error", f"Failed to add movie file:\n{e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = MoviePlayer(root)
    root.mainloop()