<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class AlertLog extends Model
{
    protected $fillable = ['event', 'temperature', 'humidity'];

    protected $casts = ['temperature' => 'float', 'humidity' => 'float'];
}
