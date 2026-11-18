<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class DeviceCommand extends Model
{
    protected $fillable = ['mode', 'outputs_on', 'version'];

    protected $casts = ['outputs_on' => 'boolean'];
}
