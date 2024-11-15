#include <pangolin/display/display.h>
#include <pangolin/plot/plotter.h>

int main(/*int argc, char* argv[]*/)
{
  // Create OpenGL window in single line
  pangolin::CreateWindowAndBind("Main",640,480);

  // Data logger object
  pangolin::DataLog log;

  // Optionally add named labels
  std::vector<std::string> labels;
  labels.push_back(std::string("sin(t)"));
  labels.push_back(std::string("cos(t)"));
  labels.push_back(std::string("sin(t)+cos(t)"));
  log.SetLabels(labels);

  // Set different line widths for each series in DataLogBlock
  const_cast<pangolin::DataLogBlock*>(log.FirstBlock())->SetLineWidth(1.0f); // First series: sin(t)
  if (const_cast<pangolin::DataLogBlock*>(log.FirstBlock())->NextBlock()) {
    const_cast<pangolin::DataLogBlock*>(log.FirstBlock())->NextBlock()->SetLineWidth(2.5f); // Second series: cos(t)
  }
  if (const_cast<pangolin::DataLogBlock*>(log.FirstBlock())->NextBlock() && 
      const_cast<pangolin::DataLogBlock*>(log.FirstBlock())->NextBlock()->NextBlock()) {
      const_cast<pangolin::DataLogBlock*>(log.FirstBlock())->NextBlock()->NextBlock()->SetLineWidth(4.0f); // Third series: sin(t)+cos(t)
  }

  const float tinc = 0.01f;

  // OpenGL 'view' of data. We might have many views of the same data.
  pangolin::Plotter plotter(&log,0.0f,4.0f*(float)M_PI/tinc,-2.0f,2.0f,(float)M_PI/(4.0f*tinc),0.5f);
  plotter.SetBounds(0.0, 1.0, 0.0, 1.0);
  plotter.Track("$i");

  // Add some sample annotations to the plot
  plotter.AddMarker(pangolin::Marker::Vertical,   -1000, pangolin::Marker::LessThan, pangolin::Colour::Blue().WithAlpha(0.2f) );
  plotter.AddMarker(pangolin::Marker::Horizontal,   100, pangolin::Marker::GreaterThan, pangolin::Colour::Red().WithAlpha(0.2f) );
  plotter.AddMarker(pangolin::Marker::Horizontal,    10, pangolin::Marker::Equal, pangolin::Colour::Green().WithAlpha(0.2f) );

  pangolin::DisplayBase().AddDisplay(plotter);

  float t = 0;

  // Default hooks for exiting (Esc) and fullscreen (tab).
  while( !pangolin::ShouldQuit() )
  {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    log.Log(sin(t),cos(t),sin(t)+cos(t));
    t += tinc;

    // Render graph, Swap frames and Process Events
    pangolin::FinishFrame();
  }

  return 0;
}
