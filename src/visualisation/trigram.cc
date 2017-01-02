/*
 * Copyright 2016 CodiLime
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include "visualisation/trigram.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <vector>
#include <cmath>

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QPixmap>
#include <QBitmap>
#include <QHBoxLayout>
#include <QGroupBox>


namespace veles {
namespace visualisation {

const int k_minimum_brightness = 25;
const int k_maximum_brightness = 103;
const double k_brightness_heuristic_threshold = 0.66;
const int k_brightness_heuristic_min = 38;
const int k_brightness_heuristic_max = 66;
// decrease this to reduce noise (but you may lose data if you overdo it)
const double k_brightness_heuristic_scaling = 2.5;

TrigramWidget::TrigramWidget(QWidget *parent) :
    VisualisationWidget(parent), texture(nullptr), databuf(nullptr), angle(0),
    c_sph(0), c_cyl(0), c_pos(0), shape_(EVisualisationShape::CUBE),
    mode_(EVisualisationMode::TRIGRAM),
    brightness_((k_maximum_brightness + k_minimum_brightness) / 2),
    pause_button_(nullptr), brightness_slider_(nullptr), is_playing_(true),
    use_brightness_heuristic_(true) {
  manipulators_.push_back(spin_manipulator_ = new SpinManipulator(this));
  manipulators_.push_back(trackball_manipulator_ = new TrackballManipulator(this));
  manipulators_.push_back(free_manipulator_ = new FreeManipulator(this));
  current_manipulator_ = nullptr;
  setManipulator(manipulators_.front());
  time_.start();
  setFocusPolicy(Qt::StrongFocus);
}

TrigramWidget::~TrigramWidget() {
  if (texture == nullptr && databuf == nullptr) return;
  makeCurrent();
  delete texture;
  delete databuf;
  doneCurrent();
}

void TrigramWidget::setBrightness(const int value) {
  brightness_ = value;
  c_brightness = static_cast<float>(value) * value * value;
  c_brightness /= getDataSize();
}

void TrigramWidget::setMode(EVisualisationMode mode, bool animate) {
  mode_ = mode;
  if (mode_ == EVisualisationMode::LAYERED_DIGRAM && !animate) {
    c_pos = 1;
  } else if (mode_ == EVisualisationMode::TRIGRAM && !animate) {
    c_pos = 0;
  }
}

float TrigramWidget::vfovDeg(float min_fov_deg, float aspect_ratio) {
  if(aspect_ratio >= 1.f) {
    return min_fov_deg;
  }

  static float deg2rad = std::acos(-1.f) / 180.f;
  float min_fov = deg2rad * min_fov_deg;
  float vfov = 2.f * std::atan(std::tan(min_fov * .5f) / aspect_ratio);
  return vfov / deg2rad;
}


void TrigramWidget::refresh() {
  if (use_brightness_heuristic_) {
    autoSetBrightness();
  }
  setBrightness(brightness_);
  makeCurrent();
  delete texture;
  delete databuf;
  initTextures();
  doneCurrent();
}

QIcon TrigramWidget::getColoredIcon(QString path, bool black_only) {
  QPixmap pixmap(path);
  QPixmap mask;
  if (black_only) {
    mask = pixmap.createMaskFromColor(QColor("black"), Qt::MaskOutColor);
  } else {
    mask = pixmap.createMaskFromColor(QColor("white"), Qt::MaskInColor);
  }
  pixmap.fill(palette().color(QPalette::WindowText));
  pixmap.setMask(mask);
  return QIcon(pixmap);
}

bool TrigramWidget::prepareOptionsPanel(QBoxLayout *layout) {
  VisualisationWidget::prepareOptionsPanel(layout);

  QLabel *brightness_label = new QLabel("Brightness: ");
  brightness_label->setAlignment(Qt::AlignTop);
  layout->addWidget(brightness_label);

  brightness_ = suggestBrightness();
  brightness_slider_ = new QSlider(Qt::Horizontal);
  brightness_slider_->setMinimum(k_minimum_brightness);
  brightness_slider_->setMaximum(k_maximum_brightness);
  brightness_slider_->setValue(brightness_);
  connect(brightness_slider_, SIGNAL(valueChanged(int)), this,
          SLOT(brightnessSliderMoved(int)));
  layout->addWidget(brightness_slider_);

  use_heuristic_checkbox_ = new QCheckBox(
    "Automatically adjust brightness");
  use_heuristic_checkbox_->setChecked(use_brightness_heuristic_);
  connect(use_heuristic_checkbox_, SIGNAL(stateChanged(int)),
          this, SLOT(setUseBrightnessHeuristic(int)));
  layout->addWidget(use_heuristic_checkbox_);

  pause_button_ = new QPushButton();
  pause_button_->setIcon(getColoredIcon(":/images/pause.png"));
  layout->addWidget(pause_button_);
  connect(pause_button_, SIGNAL(released()),
          this, SLOT(playPause()));

  QHBoxLayout *shape_box = new QHBoxLayout();
  cube_button_ = new QPushButton();
  cube_button_->setIcon(getColoredIcon(":/images/cube.png", false));
  cube_button_->setIconSize(QSize(32, 32));
  connect(cube_button_, &QPushButton::released,
          std::bind(&TrigramWidget::setShape, this,
                    EVisualisationShape::CUBE));
  shape_box->addWidget(cube_button_);

  cylinder_button_ = new QPushButton();
  cylinder_button_->setIcon(getColoredIcon(":/images/cylinder.png", false));
  cylinder_button_->setIconSize(QSize(32, 32));
  connect(cylinder_button_, &QPushButton::released,
          std::bind(&TrigramWidget::setShape, this,
                    EVisualisationShape::CYLINDER));
  shape_box->addWidget(cylinder_button_);

  sphere_button_ = new QPushButton();
  sphere_button_->setIcon(getColoredIcon(":/images/sphere.png"));
  sphere_button_->setIconSize(QSize(32, 32));

  connect(sphere_button_, &QPushButton::released,
          std::bind(&TrigramWidget::setShape, this,
                    EVisualisationShape::SPHERE));
  shape_box->addWidget(sphere_button_);

  layout->addLayout(shape_box);
  prepareManipulatorToolbar(layout);

  return true;
}

int TrigramWidget::suggestBrightness() {
  int size = getDataSize();
  auto data = reinterpret_cast<const uint8_t*>(getData());
  if (size < 100) {
    return (k_minimum_brightness + k_maximum_brightness) / 2;
  }
  std::vector<uint64_t> counts(256, 0);
  for (int i = 0; i < size; ++i) {
    counts[data[i]] += 1;
  }
  std::sort(counts.begin(), counts.end());
  int offset = 0, sum = 0;
  while (offset < 255 && sum < k_brightness_heuristic_threshold * size) {
    sum += counts[255 - offset];
    offset += 1;
  }
  offset = static_cast<int>(static_cast<double>(offset)
                            / k_brightness_heuristic_scaling);
  return std::max(k_brightness_heuristic_min,
                  k_brightness_heuristic_max - offset);
}

void TrigramWidget::playPause() {
  QPixmap pixmap;
  if (is_playing_) {
    pause_button_->setIcon(getColoredIcon(":/images/play.png"));
  } else {
    pause_button_->setIcon(getColoredIcon(":/images/pause.png"));
  }
  is_playing_ = !is_playing_;
}

void TrigramWidget::setShape(EVisualisationShape shape) {
  shape_ = shape;
}

void TrigramWidget::brightnessSliderMoved(int value) {
  if (value == brightness_) return;
  use_brightness_heuristic_ = false;
  use_heuristic_checkbox_->setChecked(false);
  setBrightness(value);
}

void TrigramWidget::setUseBrightnessHeuristic(int state) {
  use_brightness_heuristic_ = state;
  if (use_brightness_heuristic_) {
    autoSetBrightness();
  }
}

void TrigramWidget::setManipulator(Manipulator* manipulator) {
  if(manipulator == current_manipulator_) {
    return;
  }

  for (auto m : manipulators_) {
    removeEventFilter(m);
  }

  installEventFilter(manipulator);
  if (current_manipulator_ != nullptr && manipulator != nullptr) {
    manipulator->initFromMatrix(current_manipulator_->transform());
  }
  current_manipulator_ = manipulator;

  if(!is_playing_) {
    playPause();
  }

  if (pause_button_) {
    pause_button_->setEnabled(current_manipulator_->handlesPause());
  }

  emit manipulatorChanged(current_manipulator_);
  setFocus();
}

void TrigramWidget::autoSetBrightness() {
  auto new_brightness = suggestBrightness();
  if (new_brightness == brightness_) return;
  brightness_ = new_brightness;
  if (brightness_slider_ != nullptr) {
    brightness_slider_->setValue(brightness_);
  }
  setBrightness(brightness_);
}

bool TrigramWidget::event(QEvent *event) {
  if(event->type() == QEvent::MouseMove) {
    QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);
    if ((mouse_event->buttons() & Qt::LeftButton)
        && current_manipulator_ == spin_manipulator_) {
      setManipulator(trackball_manipulator_);
      trackball_manipulator_->processEvent(this, event);
    }
  } else if (event->type() == QEvent::KeyPress) {
    QKeyEvent* key_event = static_cast<QKeyEvent*>(event);
    if ((current_manipulator_ == spin_manipulator_
        || current_manipulator_ == trackball_manipulator_)
        && FreeManipulator::isCtrlButton(key_event->key())) {
      setManipulator(free_manipulator_);
      trackball_manipulator_->processEvent(this, event);
    }
  }

  return QWidget::event(event);
}

void TrigramWidget::timerEvent(QTimerEvent *e) {
  if (is_playing_) {
    angle += 0.5f;
  }

  if (shape_ == EVisualisationShape::CYLINDER) {
    c_cyl += 0.01;
  } else {
    c_cyl -= 0.01;
  }
  if (shape_ == EVisualisationShape::SPHERE) {
    c_sph += 0.01;
  } else {
    c_sph -= 0.01;
  }
  if (c_cyl > 1) c_cyl = 1;
  if (c_cyl < 0) c_cyl = 0;
  if (c_sph > 1) c_sph = 1;
  if (c_sph < 0) c_sph = 0;

  if (mode_ == EVisualisationMode::LAYERED_DIGRAM && c_pos < 1) {
    c_pos += 0.01;
    if (c_pos > 1) c_pos = 1;
  }
  if (mode_ != EVisualisationMode::LAYERED_DIGRAM && c_pos) {
    c_pos -= 0.01;
    if (c_pos < 0) c_pos = 0;
  }
  update();
}

bool TrigramWidget::initializeVisualisationGL() {
  if (!initializeOpenGLFunctions()) return false;

  glClearColor(0, 0, 0, 1);

  autoSetBrightness();

  initShaders();
  initTextures();
  initGeometry();
  setBrightness(brightness_);
  return true;
}

void TrigramWidget::initShaders() {
  if (!program.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                       ":/trigram/vshader.glsl"))
    close();

  // Compile fragment shader
  if (!program.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                       ":/trigram/fshader.glsl"))
    close();

  // Link shader pipeline
  if (!program.link()) close();

  timer.start(12, this);
}

void TrigramWidget::initTextures() {
  int size = getDataSize();
  const uint8_t *data = reinterpret_cast<const uint8_t*>(getData());

  databuf = new QOpenGLBuffer(QOpenGLBuffer::Type(GL_TEXTURE_BUFFER));
  databuf->create();
  databuf->bind();
  databuf->allocate(data, size);
  databuf->release();

  texture = new QOpenGLTexture(QOpenGLTexture::TargetBuffer);
  texture->setSize(size);
  texture->setFormat(QOpenGLTexture::R8U);
  texture->create();
  texture->bind();
  glTexBuffer(GL_TEXTURE_BUFFER, QOpenGLTexture::R8U, databuf->bufferId());
}

void TrigramWidget::initGeometry() {
  vao.create();
}

QAction* TrigramWidget::createAction(const QIcon& icon,
      Manipulator* manipulator, const QList<QKeySequence>& sequences) {
  QAction* action = new QAction(icon, manipulator->manipulatorName(), this);
  action->setShortcuts(sequences);
  connect(action, &QAction::triggered, std::bind(
      &TrigramWidget::setManipulator, this, manipulator));
  action->setProperty("manipulator", QVariant(qintptr(manipulator)));
  return action;
}

QPushButton* TrigramWidget::createActionButton(QAction* action) {
  QPushButton* button = new QPushButton;
  button->setIcon(action->icon());
  button->setToolTip(action->text());
  button->setCheckable(true);
  button->setIconSize(QSize(64, 64));
  button->setAutoExclusive(true);
  button->setProperty("action", QVariant(qintptr(action)));
  connect(button, &QPushButton::toggled, [button](bool toggled){
        if(toggled) {
          QAction* action =
              reinterpret_cast<QAction*>(
              qvariant_cast<qintptr>(
              button->property("action")));
          if(action) {
            action->trigger();
          }
        }
      });
  connect(this, &TrigramWidget::manipulatorChanged, [action, button](
      Manipulator* new_manipulator){
        Manipulator* manipulator =
            reinterpret_cast<Manipulator*>(
            qvariant_cast<qintptr>(
            action->property("manipulator")));
        if(manipulator == new_manipulator) {
          button->setChecked(true);
        }
      });
  return button;
}

void TrigramWidget::prepareManipulatorToolbar(QBoxLayout *layout) {
  QGroupBox* group = new QGroupBox;
  QHBoxLayout *group_layout = new QHBoxLayout;
  group->setTitle(tr("Camera manipulators"));
  group->setLayout(group_layout);

  {
    QAction* action = createAction(
        QIcon(":/images/manipulator_spin.png"), spin_manipulator_,
        {QKeySequence(Qt::CTRL + Qt::Key_1), QKeySequence(Qt::Key_Escape)});
    addAction(action);
    QPushButton* button = createActionButton(action);
    group_layout->addWidget(button);
    button->setChecked(true);
  }

  {
    QAction* action = createAction(
        QIcon(":/images/manipulator_trackball.png"), trackball_manipulator_,
        {QKeySequence(Qt::CTRL + Qt::Key_2)});
    addAction(action);
    QPushButton* button = createActionButton(action);
    group_layout->addWidget(button);
  }

  {
    QAction* action = createAction(
        QIcon(":/images/manipulator_free.png"), free_manipulator_,
        {QKeySequence(Qt::CTRL + Qt::Key_3)});
    addAction(action);
    QPushButton* button = createActionButton(action);
    group_layout->addWidget(button);
  }

  layout->addWidget(group);
}

void TrigramWidget::resizeGLImpl(int w, int h) {
  width = w;
  height = h;
}

void TrigramWidget::paintGLImpl() {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
  unsigned size = getDataSize();

  program.bind();
  texture->bind();
  vao.bind();

  QMatrix4x4 mp, m;
  mp.setToIdentity();
  float aspect_ratio = static_cast<float>(width) / height;
  mp.perspective(vfovDeg(45.f, aspect_ratio), aspect_ratio, 0.01f, 100.0f);

  m.setToIdentity();
  float dt = static_cast<float>(time_.restart()) / 1000.f;
  if (current_manipulator_) {
    if (is_playing_ || !current_manipulator_->handlesPause()) {
      current_manipulator_->update(dt);
    }
    m = current_manipulator_->transform();
  }

  int loc_sz = program.uniformLocation("sz");
  program.setUniformValue("tx", 0);
  program.setUniformValue("c_cyl", c_cyl);
  program.setUniformValue("c_sph", c_sph);
  program.setUniformValue("c_pos", c_pos);
  program.setUniformValue("xfrm", mp * m);
  program.setUniformValue("c_brightness", c_brightness);
  glUniform1ui(loc_sz, size);
  glDrawArrays(GL_POINTS, 0, size - 2);
}

}  // namespace visualisation
}  // namespace veles
