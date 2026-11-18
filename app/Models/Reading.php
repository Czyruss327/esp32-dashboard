<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class Reading extends Model
{
    protected $fillable = ['temperature', 'humidity', 'mode', 'alert', 'outputs_on'];

    protected $casts = [
        'temperature' => 'float',
        'humidity'    => 'float',
        'alert'       => 'boolean',
        'outputs_on'  => 'boolean',
    ];
}
