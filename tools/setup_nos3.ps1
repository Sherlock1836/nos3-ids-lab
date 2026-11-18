$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$checkout = Join-Path $root 'nos3'
$revision = '1dadc685effc26730603c36c7864afce3c546406'
$patch = Join-Path $PSScriptRoot 'nos3-integration.patch'

if (-not (Test-Path (Join-Path $checkout '.git'))) {
    git init $checkout
    if ($LASTEXITCODE) { throw 'Could not initialize the NOS3 checkout' }
    git -C $checkout remote add origin https://github.com/nasa/nos3.git
    if ($LASTEXITCODE) { throw 'Could not configure the NOS3 remote' }
    git -C $checkout fetch --depth 1 origin $revision
    if ($LASTEXITCODE) { throw 'Could not fetch the pinned NOS3 revision' }
    git -C $checkout checkout --detach FETCH_HEAD
    if ($LASTEXITCODE) { throw 'Could not check out the pinned NOS3 revision' }
}

$head = git -C $checkout rev-parse HEAD
if ($LASTEXITCODE -or $head -ne $revision) {
    throw "Expected NOS3 at $revision. Found $head in $checkout"
}
git -C $checkout -c submodule.fetchJobs=8 submodule update --init --recursive --depth 1
if ($LASTEXITCODE) { throw 'Could not initialize NOS3 submodules' }

git -C $checkout apply --reverse --check $patch 2>$null
if ($LASTEXITCODE) {
    git -C $checkout apply $patch
    if ($LASTEXITCODE) { throw 'Could not apply the IDS integration patch' }
}
