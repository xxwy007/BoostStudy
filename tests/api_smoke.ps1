param(
    [string]$BaseUrl = "http://127.0.0.1:8080"
)

$ErrorActionPreference = "Stop"

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "Assertion failed: $Message"
    }
}

$health = Invoke-RestMethod -Method Get -Uri "$BaseUrl/health"
Assert-True ($health.status -eq "ok") "health endpoint"

$created = Invoke-RestMethod -Method Post -Uri "$BaseUrl/tasks" `
    -ContentType "application/json" `
    -Body '{"title":"Learn Boost.Beast"}'
Assert-True ($created.id -gt 0) "created task has an id"
Assert-True ($created.completed -eq $false) "new task is incomplete"

$task = Invoke-RestMethod -Method Get -Uri "$BaseUrl/tasks/$($created.id)"
Assert-True ($task.title -eq "Learn Boost.Beast") "task can be fetched"

$updated = Invoke-RestMethod -Method Put -Uri "$BaseUrl/tasks/$($created.id)" `
    -ContentType "application/json" `
    -Body '{"completed":true}'
Assert-True ($updated.completed -eq $true) "task can be completed"

$allTasks = @(Invoke-RestMethod -Method Get -Uri "$BaseUrl/tasks")
Assert-True ($allTasks.Count -ge 1) "task list is not empty"

Invoke-RestMethod -Method Delete -Uri "$BaseUrl/tasks/$($created.id)"

$stats = Invoke-RestMethod -Method Get -Uri "$BaseUrl/stats"
Assert-True ($stats.total_requests -ge 7) "request statistics are updated"

Write-Host "All API smoke tests passed." -ForegroundColor Green

