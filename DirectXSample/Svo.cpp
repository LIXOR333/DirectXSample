using System;
using UnityEngine;
using System.Runtime.InteropServices;

namespace RustCheat236
{
    public class RustCheat236 : MonoBehaviour
    {
        private static IntPtr playerBase = new IntPtr(0x0178F2D0);
        private static IntPtr viewMatrix = new IntPtr(0x01A3B4C8);
        private static IntPtr entityList = new IntPtr(0x0179A1B8);
        private static int healthOffset = 0x1F8;
        private static int posOffset = 0x2A0;

        private bool showConsole = true;
        private Rect consoleRect = new Rect(10, 10, 80, 40);
        private bool aimbotEnabled = false;
        private bool wallhackEnabled = false;
        private float aimSmoothness = 0.2f;
        private float aimFov = 10.0f;

        [DllImport("user32.dll")]
        private static extern short GetAsyncKeyState(int vKey);

        [DllImport("kernel32.dll")]
        private static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int nSize, out int lpNumberOfBytesRead);

        void Start()
        {
            Application.targetFrameRate = 60;
            QualitySettings.vSyncCount = 0;
            Debug.Log("Rust Cheat 236 Loaded!");
        }

        void Update()
        {
            if (GetAsyncKeyState(0x70) != 0) // F1
            {
                aimbotEnabled = !aimbotEnabled;
                Debug.Log("Аимбот: " + (aimbotEnabled ? "ВКЛ" : "ВЫКЛ"));
            }
            if (GetAsyncKeyState(0x71) != 0) // F2
            {
                wallhackEnabled = !wallhackEnabled;
                Debug.Log("Wallhack: " + (wallhackEnabled ? "ВКЛ" : "ВЫКЛ"));
            }
            if (aimbotEnabled) RunAimbot();
            if (wallhackEnabled) RunWallhack();
        }

        void OnGUI()
        {
            if (showConsole)
            {
                GUI.color = new Color(1, 1, 1, 0);
                GUILayout.BeginArea(consoleRect);

                GUIStyle labelStyle = new GUIStyle(GUI.skin.label)
                {
                    fontSize = 7,
                    normal = { textColor = new Color(1, 1, 1, 0.6f) }
                };

                GUILayout.Label(aimbotEnabled ? "A: ON" : "A: OFF", labelStyle);
                aimSmoothness = GUILayout.HorizontalSlider(aimSmoothness, 0.1f, 1.0f, GUILayout.Width(60));
                GUILayout.Label("S:" + aimSmoothness.ToString("F1"), labelStyle);
                aimFov = GUILayout.HorizontalSlider(aimFov, 5.0f, 20.0f, GUILayout.Width(60));
                GUILayout.Label("F:" + aimFov.ToString("F0"), labelStyle);
                GUILayout.Label(wallhackEnabled ? "W: ON" : "W: OFF", labelStyle);

                GUILayout.EndArea();
            }
        }

        void RunAimbot()
        {
            Vector3 target = GetClosestEnemy();
            if (target != Vector3.zero)
            {
                Vector3 aimDirection = (target - Camera.main.transform.position).normalized;
                Quaternion targetRotation = Quaternion.LookRotation(aimDirection);
                Camera.main.transform.rotation = Quaternion.Slerp(
                    Camera.main.transform.rotation,
                    targetRotation,
                    aimSmoothness * Time.deltaTime
                );
            }
        }

        void RunWallhack()
        {
            foreach (var player in FindObjectsOfType<Player>())
            {
                Vector3 screenPos = Camera.main.WorldToScreenPoint(player.transform.position);
                if (screenPos.z > 0)
                {
                    GUI.color = new Color(1, 0, 0, 0.6f);
                    GUIStyle espStyle = new GUIStyle(GUI.skin.label) { fontSize = 7 };
                    GUI.Label(new Rect(screenPos.x, Screen.height - screenPos.y, 40, 10), player.name, espStyle);
                }
            }
        }

        Vector3 GetClosestEnemy()
        {
            IntPtr processHandle = Process.GetCurrentProcess().Handle;
            byte[] buffer = new byte[sizeof(float) * 3];
            int bytesRead;
            ReadProcessMemory(processHandle, entityList + posOffset, buffer, buffer.Length, out bytesRead);
            float x = BitConverter.ToSingle(buffer, 0);
            float y = BitConverter.ToSingle(buffer, 4);
            float z = BitConverter.ToSingle(buffer, 8);
            return new Vector3(x, y, z);
        }
    }
}
