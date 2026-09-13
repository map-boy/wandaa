Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$form = New-Object System.Windows.Forms.Form
$form.Text = "Wandaa Control Panel"
$form.Size = New-Object System.Drawing.Size(600,500)
$form.StartPosition = "CenterScreen"

$output = New-Object System.Windows.Forms.TextBox
$output.Multiline = $true
$output.ScrollBars = "Vertical"
$output.ReadOnly = $true
$output.Font = New-Object System.Drawing.Font("Consolas", 9)
$output.Location = New-Object System.Drawing.Point(10, 260)
$output.Size = New-Object System.Drawing.Size(560, 190)
$form.Controls.Add($output)

$commands = @(
    @{ Label = "New Project (tangira)"; Cmd = "tangira demo" },
    @{ Label = "Build (yubaka)"; Cmd = "yubaka" },
    @{ Label = "Run Tests (gerageza)"; Cmd = "gerageza" },
    @{ Label = "Version (verisiyo)"; Cmd = "verisiyo" },
    @{ Label = "Server [not implemented]"; Cmd = "seriveri" },
    @{ Label = "Database [not implemented]"; Cmd = "nyabubiko" },
    @{ Label = "ML / AI [not implemented]"; Cmd = "ubwenge" }
)

$y = 10
foreach ($c in $commands) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text = $c.Label
    $btn.Size = New-Object System.Drawing.Size(270, 30)
    $btn.Location = New-Object System.Drawing.Point(10, $y)
    $cmdCopy = $c.Cmd
    $btn.Add_Click({
        param($sender, $eventArgs)
        $output.AppendText("`r`n> wandaa $cmdCopy`r`n")
        $result = & .\bin\wandaa.exe @($cmdCopy -split " ") 2>&1 | Out-String
        $output.AppendText($result)
    }.GetNewClosure())
    $form.Controls.Add($btn)
    $y += 35
}

[void]$form.ShowDialog()
